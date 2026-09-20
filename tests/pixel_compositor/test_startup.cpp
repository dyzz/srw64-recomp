// Make backend failures diagnosable in headless CI; never suppress a failure.
#include <cstdio>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
namespace {
LONG WINAPI report_exception(EXCEPTION_POINTERS* error) {
    std::fprintf(stderr, "GPU test exception: 0x%lX at %p\n", error->ExceptionRecord->ExceptionCode,
                 error->ExceptionRecord->ExceptionAddress);
    void* frames[32]{};
    const auto count = CaptureStackBackTrace(0, 32, frames, nullptr);
    const auto process = GetCurrentProcess();
    if (SymInitialize(process, nullptr, TRUE)) {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
        for (USHORT i = 0; i < count; ++i) {
            DWORD64 offset{};
            if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &offset, symbol))
                std::fprintf(stderr, "  %s + 0x%llX\n", symbol->Name, static_cast<unsigned long long>(offset));
            else std::fprintf(stderr, "  %p\n", frames[i]);
        }
        SymCleanup(process);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
}
#endif
namespace {
struct Startup {
    Startup() {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        std::cout << std::unitbuf;
#ifdef _WIN32
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        SetUnhandledExceptionFilter(report_exception);
#endif
        std::fputs("Starting real offscreen compositor test\n", stderr);
    }
} startup;
}
