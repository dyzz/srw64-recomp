// A minidump when the program crashes on Windows (crash_dump.hpp), for the bug report:
// the stacks of all threads and the memory they point at, a few MB, no full memory.
// Everything a crash needs is made in install() and attach(): the folder's name, a
// thread that writes the dump (a crashed thread may have no stack left, and dbghelp
// wants plenty), the events it waits on. The crashed thread only hands its exception over.
//
// Not covered: __fastfail (0xC0000409: /GS cookie failures, the CRT's own fast-fail paths
// past these handlers) ends the process without running anything in it; Event Viewer's
// Application Error entry is all there is. A debugger attached sees the exception first.
#include "../crash_dump.hpp"
#include "../console_log.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>
#include <io.h>

namespace srw64::crash_dump {
namespace {
// The CRT paths that do not raise an exception get one of these codes in the dump.
constexpr DWORD kInvalidParameter = 0xC0000417;   // STATUS_INVALID_CRUNTIME_PARAMETER
constexpr DWORD kPureCall = 0xE0535201;
constexpr DWORD kAbort = 0xE0535202;
constexpr DWORD kTerminate = 0xE0535203;
// How long a crashed thread waits for the dump before the process goes on dying.
constexpr DWORD kWait = 20'000;

wchar_t folder[1024];                 // the run directory
std::atomic<bool> attached{false};
HANDLE wake{}, finished{};            // to the writer; from it (manual reset)
DWORD writer_id = 0;
std::atomic<int> state{0};            // 0 nothing yet, 1 writing, 2 written
EXCEPTION_POINTERS* crash = nullptr;
DWORD crash_thread = 0;
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
_crt_signal_t previous_abort = SIG_DFL;

// The SRW64_CRASH line, put together without the heap.
struct Line {
    char text[1024];
    size_t size = 0;
    void add(const char* s) { while (*s && size + 1 < sizeof(text)) text[size++] = *s++; }
    void add(const wchar_t* s) {
        // UTF-8 for the module's name.
        const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, text + size, int(sizeof(text) - size - 1), nullptr, nullptr);
        if (n > 0) size += size_t(n) - 1;
    }
    void hex(uint64_t value, int digits) {
        char digit[17];
        for (int i = digits - 1; i >= 0; --i, value >>= 4) digit[i] = "0123456789ABCDEF"[value & 15];
        digit[digits] = 0;
        add("0x");
        add(digit);
    }
    void dec(uint64_t value) {
        char digit[21];
        int at = 20;
        digit[at] = 0;
        do digit[--at] = char('0' + value % 10); while (value /= 10);
        add(digit + at);
    }
};

const char* name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer division by zero";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case 0xE06D7363: return "C++ exception not caught";
    case kInvalidParameter: return "CRT invalid parameter";
    case kPureCall: return "pure virtual call";
    case kAbort: return "abort";
    case kTerminate: return "std::terminate";
    default: return "exception";
    }
}

void put(int fd, const Line& line) {
    if (fd < 0) return;
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    DWORD written = 0;
    if (handle != INVALID_HANDLE_VALUE) WriteFile(handle, line.text, DWORD(line.size), &written, nullptr);
}

// On the writer thread: the dump, then what the pipes still hold, then the line.
void write_report() {
    static wchar_t path[std::size(folder) + 40];
    static wchar_t file[40];
    SYSTEMTIME now{};
    GetLocalTime(&now);
    swprintf(file, std::size(file), L"crash-%04u%02u%02u-%02u%02u%02u.dmp", now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);
    uint64_t size = 0;
    bool written = false;
    if (attached.load(std::memory_order_acquire)) {
        swprintf(path, std::size(path), L"%ls\\%ls", folder, file);
        const HANDLE out = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{crash_thread, crash, FALSE};
            // Stacks, and 1 KB around what they point at: locals and the objects they use.
            const auto type = MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                            MiniDumpWithUnloadedModules | MiniDumpIgnoreInaccessibleMemory);
            written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), out, type, &exception, nullptr, nullptr);
            LARGE_INTEGER length{};
            if (written && GetFileSizeEx(out, &length)) size = uint64_t(length.QuadPart);
            CloseHandle(out);
            if (!written) DeleteFileW(path);
        }
    }
    console_log::flush_now();
    const EXCEPTION_RECORD& record = *crash->ExceptionRecord;
    const auto address = reinterpret_cast<uintptr_t>(record.ExceptionAddress);
    Line line;
    line.add("SRW64_CRASH ");
    line.add(name(record.ExceptionCode));
    line.add(" ");
    line.hex(record.ExceptionCode, 8);
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
        const auto how = record.ExceptionInformation[0];
        line.add(how == 0 ? " reading " : how == 1 ? " writing " : " executing ");
        line.hex(record.ExceptionInformation[1], 16);
    }
    line.add(" at ");
    line.hex(address, 16);
    HMODULE module{};
    static wchar_t module_path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) &&
        GetModuleFileNameW(module, module_path, MAX_PATH)) {
        const wchar_t* base = module_path;
        for (const wchar_t* at = module_path; *at; ++at)
            if (*at == L'\\' || *at == L'/') base = at + 1;
        line.add(" (");
        line.add(base);
        line.add("+");
        line.hex(address - reinterpret_cast<uintptr_t>(module), 8);
        line.add(")");
    }
    line.add(" thread ");
    line.dec(crash_thread);
    if (written) {
        line.add(", ");
        line.add(file);
        line.add(" (");
        line.dec(size);
        line.add(" bytes)");
    } else {
        line.add(attached ? ", no dump (" : ", no dump (no run directory yet");
        if (attached) { line.dec(GetLastError()); line.add(")"); }
        else line.add(")");
    }
    line.add("\n");
    put(console_log::original_stderr(), line);
    put(console_log::crash_fd(), line);
}

DWORD WINAPI writer_main(void*) {
    WaitForSingleObject(wake, INFINITE);
    write_report();
    SetEvent(finished);
    return 0;
}

// Once per process: the first thread to crash hands over and waits; one that crashes
// meanwhile waits too; later ones (abort() after the filter's terminate) go on.
void report(EXCEPTION_POINTERS* pointers) {
    if (GetCurrentThreadId() == writer_id) return;   // dbghelp itself failed
    int expected = 0;
    if (!state.compare_exchange_strong(expected, 1)) {
        if (expected == 1) WaitForSingleObject(finished, kWait);
        return;
    }
    crash = pointers;
    crash_thread = GetCurrentThreadId();
    if (wake) {
        SetEvent(wake);
        WaitForSingleObject(finished, kWait);
    } else {
        write_report();
    }
    state = 2;
}

// The CRT handlers are plain calls: the dump's exception is this context.
__declspec(noinline) void report_here(DWORD code) {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = code;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress = _ReturnAddress();
    EXCEPTION_POINTERS pointers{&record, &context};
    report(&pointers);
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* pointers) {
    report(pointers);
    // The CRT's filter (a C++ exception: std::terminate), else Windows Error Reporting,
    // which still makes Event Viewer's Application Error entry.
    return previous_filter ? previous_filter(pointers) : EXCEPTION_CONTINUE_SEARCH;
}

void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    report_here(kInvalidParameter);
    // The CRT's own handler ends the process so; returning would let the call go on.
    __fastfail(FAST_FAIL_INVALID_ARG);
}

void on_purecall() { report_here(kPureCall); }   // the CRT calls abort() next

// Per thread in the MSVC runtime: this is the main thread's; another thread's
// std::terminate reaches abort(), so SIGABRT below.
void on_terminate() {
    report_here(kTerminate);
    std::abort();
}

// SIGABRT is one handler for the process: this, then console_log's (which re-raises).
void on_abort(int signal) {
    report_here(kAbort);
    if (previous_abort != SIG_DFL && previous_abort != SIG_IGN && previous_abort != SIG_ERR) {
        previous_abort(signal);
    } else {
        std::signal(signal, SIG_DFL);
        std::raise(signal);
    }
}

int overflow(int depth) {
    volatile char pad[4096];
    pad[0] = char(depth);
    static int (*volatile again)(int) = overflow;
    return again(depth + 1) + pad[0];
}

void crash_test(std::string_view kind) {
    std::fprintf(stderr, "SRW64_CRASH_TEST %.*s\n", int(kind.size()), kind.data());
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);   // no dialog on a test machine
    if (kind == "access") {
        volatile uintptr_t bad = 0x10;
        *reinterpret_cast<volatile int*>(bad) = 1;
    } else if (kind == "stack") {
        // On a thread of its own, as the game's threads: no stack guarantee.
        std::thread([] { overflow(0); }).join();
    } else if (kind == "abort") {
        std::abort();
    } else if (kind == "invalid") {
        // 0.4.0's crash: the UCRT takes a buffer size of 0 as an invalid parameter.
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
    } else if (kind == "terminate") {
        std::terminate();
    } else if (kind == "throw") {
        std::thread([] { throw std::runtime_error("SRW64_CRASH_TEST"); }).join();
    }
    std::fprintf(stderr, "SRW64_CRASH_TEST %.*s: still running\n", int(kind.size()), kind.data());
}
}

void install() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (wake && finished) {
        if (const HANDLE writer = CreateThread(nullptr, 256 << 10, writer_main, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, &writer_id))
            CloseHandle(writer);
        else
            wake = nullptr;
    }
    previous_filter = SetUnhandledExceptionFilter(on_exception);
    _set_invalid_parameter_handler(on_invalid_parameter);
    _set_purecall_handler(on_purecall);
    std::set_terminate(on_terminate);
    previous_abort = std::signal(SIGABRT, on_abort);
}

void attach(const std::filesystem::path& run) {
    const std::wstring& text = run.native();
    if (text.size() < std::size(folder)) {
        wmemcpy(folder, text.c_str(), text.size() + 1);
        attached.store(true, std::memory_order_release);
    }
    if (const char* kind = std::getenv("SRW64_CRASH_TEST"); kind && *kind) crash_test(kind);
}
}
