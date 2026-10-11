// Starting Marchwind64.exe by double-click, as Marchwind64.cmd does (host.cpp main()).
#include "app/desktop.hpp"
#include <cstdlib>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <stdexcept>
#include <string>
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
void windows_release_console() {
    // A console the program shares (cmd, PowerShell, Marchwind64.cmd) keeps its output;
    // one opened just for this process by Explorer closes. Called first thing in main,
    // before anything opens a handle: FreeConsole closes the console's handles by the
    // numbers they had at start, and after console_log's redirection had freed those
    // numbers it closed whatever had taken them since (0.4.5: crash_dump's events, the
    // writer then woke with no crash and crashed reading it).
    DWORD ids[2];
    if (GetConsoleProcessList(ids, 2) != 1) return;
    FreeConsole();
    // stdin, stdout and stderr still name the closed handles: NUL instead, so console_log
    // redirects real ones. Closing a dead handle fails harmlessly, and nothing has been
    // opened since to take its number; each _open takes the descriptor just freed. A
    // descriptor that was never open would be an invalid parameter, which ends the
    // process before crash_dump is there: this thread ignores it meanwhile.
    const auto previous = _set_thread_local_invalid_parameter_handler(
        [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {});
    for (int fd = 0; fd <= 2; ++fd) {
        _close(fd);
        const int nul = _open("NUL", fd == 0 ? _O_RDONLY : _O_WRONLY);
        if (nul >= 0 && nul != fd) { _dup2(nul, fd); _close(nul); }
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
