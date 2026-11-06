// Starting Marchwind64.exe by double-click, as Marchwind64.cmd does (host.cpp main()).
#include "app/desktop.hpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <io.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>

namespace srw64::app {
namespace {
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
    if (size <= 0) return L"The application could not display the error text.";
    std::wstring result(size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), result.data(), size);
    return result;
}
}
fs::path windows_named_rom() {
    const auto* value = _wgetenv(L"SRW64_ROM");
    return value && *value ? fs::path(value) : fs::path{};
}
void windows_connect_streams() {
    // A windows-subsystem program: no console window of its own, from Explorer or a
    // launcher alike. What started it decides the rest. Handles it was given (a pipe, a
    // file) are already in place. A shell's console is joined, so Marchwind64.cmd and
    // --help still print there. Anything left is NUL: console_log redirects descriptors 1
    // and 2, so the streams have to be real and on those numbers. The CRT holds 0 to 2
    // open with no handle behind them, and a reopened stream would land on a new number
    // and print past console_log, so each is closed first; closing one that has no handle
    // is an invalid parameter, which would end the process, so this thread ignores it.
    const bool console = AttachConsole(ATTACH_PARENT_PROCESS);
    const auto previous = _set_thread_local_invalid_parameter_handler(
        [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {});
    FILE* reopened = nullptr;
    if (_fileno(stdin) < 0) {
        _close(0);
        freopen_s(&reopened, "NUL", "r", stdin);
    }
    for (const auto& [stream, descriptor] : {std::pair{stdout, 1}, std::pair{stderr, 2}}) {
        if (_fileno(stream) >= 0) continue;
        _close(descriptor);
        if (!console || freopen_s(&reopened, "CONOUT$", "w", stream) != 0) freopen_s(&reopened, "NUL", "w", stream);
    }
    _set_thread_local_invalid_parameter_handler(previous);
}
DesktopUi windows_desktop_ui() {
    return {
        []() -> std::optional<fs::path> {
            std::wstring file(32768, L'\0');
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = L"N64 ROM (*.z64; *.v64; *.n64)\0*.z64;*.v64;*.n64\0*.*\0*.*\0";
            dialog.lpstrFile = file.data();
            dialog.nMaxFile = DWORD(file.size());
            dialog.lpstrTitle = L"选择 ROM：超级机器人大战 64（日版 Rev 0） — "
                                L"Select your Super Robot Taisen 64 ROM (Japan, Rev 0)";
            // Identity is checked by SHA-256, not the extension or file name.
            dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                           OFN_DONTADDTORECENT | OFN_HIDEREADONLY;
            // The shell's dialog wants an apartment; balanced, so SDL later starts from none.
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            const BOOL chosen = GetOpenFileNameW(&dialog);
            if (SUCCEEDED(com)) CoUninitialize();
            if (!chosen) {
                if (const auto failure = CommDlgExtendedError())
                    throw std::runtime_error("The ROM dialog failed (" + std::to_string(failure) + ")");
                return std::nullopt; // Cancel
            }
            file.resize(wcslen(file.c_str()));
            return fs::path(file);
        },
        [](const std::string& message) {
            MessageBoxW(nullptr, wide(message).c_str(), L"Marchwind64",
                        MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        },
        // A message box names its buttons in the system's language (OK, Cancel), so the
        // question reads without the labels. Cancel is the default.
        [](const std::string& message, const std::string&, const std::string&) {
            return MessageBoxW(nullptr, wide(message).c_str(), L"Marchwind64",
                               MB_OKCANCEL | MB_DEFBUTTON2 | MB_ICONWARNING | MB_SETFOREGROUND) == IDOK;
        }
    };
}
}
