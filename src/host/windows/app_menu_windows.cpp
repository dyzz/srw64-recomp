// The Windows menu bar (app_menu.hpp): always on the window, full screen included. SDL turns
// Alt and F10 away from menus, so the bar answers the mouse; the shortcuts it names (Ctrl+,
// F5, F11) are frontend.cpp's own keys. Everything runs on the window's thread.
#include "app_menu.hpp"
#include <SDL.h>
#include <SDL_syswm.h>
#include <atomic>
#include <iterator>
#include <string>
#include <windows.h>

namespace srw64::app_menu {
namespace {
enum : UINT { kSettings = 1, kReload, kUpdates, kAbout, kExit, kFullscreen = 10, kScale1 = 11 };
std::atomic_bool requested{}, reload{}, fullscreen{}, about{}, updates{};
std::atomic_int scale{};
HWND hwnd{};
HMENU bar{}, game{}, view{};
WNDPROC sdl_proc{};
bool installed = false;
std::string last_title;
WindowState last_state{false, -1, -1};

std::wstring wide(const std::string& value) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
    std::wstring result(size > 0 ? size_t(size) : 0, L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), result.data(), size);
    return result;
}
std::string scale_title(const std::string& pattern, int n) {
    std::string title = pattern;
    if (const size_t at = title.find("{n}"); at != std::string::npos) title.replace(at, 3, std::to_string(n));
    return title;
}
void set_text(HMENU menu, UINT id, const std::string& text, bool by_position = false) {
    std::wstring value = wide(text);
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = value.data();
    SetMenuItemInfoW(menu, id, by_position, &info);
}
LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND && HIWORD(w) == 0) {
        const UINT id = LOWORD(w);
        if (id >= kScale1 && id < kScale1 + kScales) { scale = int(id - kScale1 + 1); return 0; }
        switch (id) {
        case kSettings: requested = true; return 0;
        case kReload: reload = true; return 0;
        case kUpdates: updates = true; return 0;
        case kAbout: about = true; return 0;
        case kFullscreen: fullscreen = true; return 0;
        // As the window's close button: SDL's own close event, then its quit.
        case kExit: PostMessageW(window, WM_CLOSE, 0, 0); return 0;
        }
    }
    return CallWindowProcW(sdl_proc, window, message, w, l);
}
}

void attach(SDL_Window* window) {
    if (hwnd || !window) return;
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return;
    hwnd = info.info.win.window;
    bar = CreateMenu();
    game = CreatePopupMenu();
    view = CreatePopupMenu();
    // Titles come with the first update(); the bar is here before the renderer sizes its
    // swap chain, so the picture never starts under it.
    AppendMenuW(game, MF_STRING, kSettings, L"Settings");
    AppendMenuW(game, MF_STRING, kReload, L"Reload");
    AppendMenuW(game, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(game, MF_STRING, kUpdates, L"Updates");
    AppendMenuW(game, MF_STRING, kAbout, L"About");
    AppendMenuW(game, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(game, MF_STRING, kExit, L"Exit");
    AppendMenuW(view, MF_STRING, kFullscreen, L"Full Screen");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    for (int n = 1; n <= kScales; ++n) AppendMenuW(view, MF_STRING, kScale1 + n - 1, std::to_wstring(n).c_str());
    AppendMenuW(bar, MF_POPUP, UINT_PTR(game), L"Marchwind64");
    AppendMenuW(bar, MF_POPUP, UINT_PTR(view), L"View");
    sdl_proc = WNDPROC(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(&proc)));
    // The bar takes its height from the window; the picture keeps the size it was given.
    RECT before{}, after{}, frame{};
    GetClientRect(hwnd, &before);
    SetMenu(hwnd, bar);
    GetClientRect(hwnd, &after);
    const bool sized = !(SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED));
    if (sized && GetWindowRect(hwnd, &frame) && after.bottom != before.bottom)
        SetWindowPos(hwnd, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top + before.bottom - after.bottom,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
void update(const Labels& labels, const WindowState& state) {
    if (!hwnd) return;
    const std::string title = labels.game + "\n" + labels.settings + "\n" + labels.reload + "\n" + labels.view + "\n" + labels.fullscreen +
                              "\n" + labels.window_scale + "\n" + labels.about + "\n" + labels.check_updates + "\n" + labels.exit;
    if (installed && title == last_title && state.fullscreen == last_state.fullscreen &&
        state.scale == last_state.scale && state.largest == last_state.largest) return;
    set_text(bar, 0, labels.game, true);
    set_text(bar, 1, labels.view, true);
    set_text(game, kSettings, labels.settings + "\tCtrl+,");
    set_text(game, kReload, labels.reload + "\tF5");
    set_text(game, kUpdates, labels.check_updates);
    set_text(game, kAbout, labels.about);
    set_text(game, kExit, labels.exit + "\tAlt+F4");
    set_text(view, kFullscreen, labels.fullscreen + "\tF11");
    CheckMenuItem(view, kFullscreen, state.fullscreen ? MF_CHECKED : MF_UNCHECKED);
    for (int n = 1; n <= kScales; ++n) {
        const UINT id = kScale1 + n - 1;
        set_text(view, id, scale_title(labels.window_scale, n));
        CheckMenuItem(view, id, state.scale == n ? MF_CHECKED : MF_UNCHECKED);
        EnableMenuItem(view, id, !state.fullscreen && n <= state.largest ? MF_ENABLED : MF_GRAYED);
    }
    DrawMenuBar(hwnd);
    installed = true;
    last_title = title;
    last_state = state;
}
bool available() { return installed; }
bool activate_settings() {
    if (!installed) return false;
    SendMessageW(hwnd, WM_COMMAND, kSettings, 0);
    return true;
}
bool activate(const std::string& title) {
    if (!installed) return false;
    const std::wstring wanted = wide(title);
    for (int i = 0, count = GetMenuItemCount(view); i < count; ++i) {
        wchar_t text[256]{};
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING | MIIM_ID | MIIM_STATE | MIIM_FTYPE;
        info.dwTypeData = text;
        info.cch = UINT(std::size(text) - 1);
        if (!GetMenuItemInfoW(view, UINT(i), TRUE, &info) || (info.fType & MFT_SEPARATOR) || (info.fState & MFS_DISABLED)) continue;
        std::wstring name(text);
        name = name.substr(0, name.find(L'\t'));  // without the shortcut
        if (name != wanted) continue;
        SendMessageW(hwnd, WM_COMMAND, info.wID, 0);
        return true;
    }
    return false;
}
bool take_settings_request() { return requested.exchange(false); }
bool take_about_request() { return about.exchange(false); }
bool take_update_request() { return updates.exchange(false); }
bool take_reload_request() { return reload.exchange(false); }
bool take_fullscreen_request() { return fullscreen.exchange(false); }
int take_scale_request() { return scale.exchange(0); }
void shutdown() {
    // A destroyed window took its menu with it.
    if (hwnd && IsWindow(hwnd)) {
        if (WNDPROC(GetWindowLongPtrW(hwnd, GWLP_WNDPROC)) == &proc) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(sdl_proc));
        SetMenu(hwnd, nullptr);
        DestroyMenu(bar);  // with its popups
    }
    hwnd = nullptr; bar = game = view = nullptr; sdl_proc = nullptr;
    installed = false; requested = false; reload = false; fullscreen = false; scale = 0; about = false; updates = false;
    last_title.clear(); last_state = {false, -1, -1};
}
}
