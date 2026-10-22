// The Windows menu bar (app_menu.hpp): always on a window; in full screen it comes down when
// the mouse reaches the top of the screen and goes away when the mouse leaves it. SDL turns
// Alt and F10 away from menus, so the bar answers the mouse; the shortcuts it names (Ctrl+,
// F5, F11, F12, the bound keys) are the frontend's and graphics.cpp's own keys. Everything
// runs on the window's thread.
#include "app_menu.hpp"
#include <SDL.h>
#include <SDL_syswm.h>
#include <atomic>
#include <deque>
#include <iterator>
#include <string>
#include <vector>
#include <windows.h>
#include <shellapi.h>

namespace srw64::app_menu {
namespace {
// The first menu's own entries; update()'s entries are kFirstEntry + their place in `ids`.
// About and Check for Updates are Help's entries here, as Windows applications have them.
enum : UINT { kSettings = 1, kReload, kTitle, kExit, kFirstEntry = 100 };
std::atomic_bool requested{}, reload{};
std::deque<std::string> chosen;  // ids of entries chosen, for take_request()
std::vector<std::string> ids;    // by command id - kFirstEntry
HWND hwnd{};
HMENU bar{}, game{};
WNDPROC sdl_proc{};
bool installed = false;
bool bar_shown = false;
std::string last_labels, last_menus, last_shape;
std::vector<Menu> model;  // as last shown, for activate()

std::wstring wide(const std::string& value) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
    std::wstring result(size > 0 ? size_t(size) : 0, L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), result.data(), size);
    return result;
}
void set_text(HMENU menu, UINT id, const std::string& text, bool by_position = false) {
    std::wstring value = wide(text);
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = value.data();
    SetMenuItemInfoW(menu, id, by_position, &info);
}
// Puts the bar on the window or takes it off. A window keeps its picture's size (the bar
// adds its height to the window); in full screen the window is the screen and the picture
// gives the bar its row.
void show_bar(bool shown, bool keep_picture) {
    if (shown == bar_shown) return;
    RECT before{}, after{}, frame{};
    GetClientRect(hwnd, &before);
    SetMenu(hwnd, shown ? bar : nullptr);
    bar_shown = shown;
    GetClientRect(hwnd, &after);
    if (keep_picture && GetWindowRect(hwnd, &frame) && after.bottom != before.bottom)
        SetWindowPos(hwnd, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top + before.bottom - after.bottom,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
// In full screen: shown while the mouse is at the screen's top edge or on the bar.
bool bar_wanted(bool fullscreen) {
    if (!fullscreen) return true;
    if (GetForegroundWindow() != hwnd) return false;
    POINT mouse{};
    RECT frame{};
    if (!GetCursorPos(&mouse) || !GetWindowRect(hwnd, &frame) || mouse.x < frame.left || mouse.x >= frame.right) return false;
    if (!bar_shown) return mouse.y <= frame.top;
    POINT client{0, 0};
    ClientToScreen(hwnd, &client);
    return mouse.y < client.y;
}
// What the entries say and whether they are on (last_menus), and their ids alone
// (last_shape): a change of shape builds the menus again, any other is set in place.
void describe(const std::vector<Item>& items, std::string& all, std::string& shape) {
    for (const auto& i : items) {
        shape += i.id + "{";
        all += i.id + "\x1f" + i.title + "\x1f" + i.shortcut + (i.checked ? "+" : "-") + (i.enabled ? "+" : "-") + "{";
        describe(i.items, all, shape);
        shape += "}"; all += "}";
    }
}
void fill(HMENU menu, const std::vector<Item>& items) {
    for (const auto& i : items) {
        if (i.id.empty()) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        else if (!i.items.empty()) {
            HMENU sub = CreatePopupMenu();
            fill(sub, i.items);
            AppendMenuW(menu, MF_POPUP, UINT_PTR(sub), L"-");
        } else {
            AppendMenuW(menu, MF_STRING, kFirstEntry + UINT(ids.size()), L"-");
            ids.push_back(i.id);
        }
    }
}
void apply(HMENU menu, const std::vector<Item>& items) {
    for (UINT n = 0; n < items.size(); ++n) {
        const Item& i = items[n];
        if (i.id.empty()) continue;
        set_text(menu, n, i.shortcut.empty() ? i.title : i.title + "\t" + i.shortcut, true);
        CheckMenuItem(menu, n, MF_BYPOSITION | (i.checked ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menu, n, MF_BYPOSITION | (i.enabled ? MF_ENABLED : MF_GRAYED));
        if (HMENU sub = GetSubMenu(menu, int(n))) apply(sub, i.items);
    }
}
// A menu bar entry has text only: an icon is a character before the title, as the menu
// font shows it.
std::string bar_title(const Menu& menu) {
    if (menu.icon == "globe") return "\xF0\x9F\x8C\x90 " + menu.title;  // U+1F310 GLOBE WITH MERIDIANS
    return menu.title;
}
const Item* titled(const std::vector<Item>& items, const std::string& title) {
    for (const auto& i : items) {
        if (!i.id.empty() && i.items.empty() && i.title == title) return &i;
        if (const Item* inner = titled(i.items, title)) return inner;
    }
    return nullptr;
}
LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND && HIWORD(w) == 0) {
        const UINT id = LOWORD(w);
        if (id >= kFirstEntry && id - kFirstEntry < ids.size()) { chosen.push_back(ids[id - kFirstEntry]); return 0; }
        switch (id) {
        case kSettings: requested = true; return 0;
        case kReload: reload = true; return 0;
        case kTitle: chosen.push_back("title-return"); return 0;
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
    // Titles come with the first update(), and the other menus after this one; the bar is
    // here before the renderer sizes its swap chain, so the picture never starts under it.
    AppendMenuW(game, MF_STRING, kSettings, L"Settings");
    AppendMenuW(game, MF_STRING, kReload, L"Reload");
    AppendMenuW(game, MF_STRING, kTitle, L"Title");
    AppendMenuW(game, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(game, MF_STRING, kExit, L"Exit");
    AppendMenuW(bar, MF_POPUP, UINT_PTR(game), L"Marchwind64");
    sdl_proc = WNDPROC(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(&proc)));
    show_bar(true, !(SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED)));
}
void update(const Labels& labels, const std::vector<Menu>& menus, bool fullscreen) {
    if (!hwnd) return;
    // Leaving full screen, SDL sized the window without the bar: it comes back with its height.
    show_bar(bar_wanted(fullscreen), !fullscreen && !IsZoomed(hwnd));
    const std::string label_text = labels.game + "\n" + labels.settings + "\n" + labels.reload + "\n" + labels.exit +
        "\n" + labels.title_return + (labels.title_enabled ? "+" : "-");
    std::string all, shape;
    for (const auto& menu : menus) {
        all += menu.title + "\x1f" + menu.icon + "[";
        shape += "[";
        describe(menu.items, all, shape);
    }
    if (installed && label_text == last_labels && all == last_menus) return;
    set_text(bar, 0, labels.game, true);
    set_text(game, kSettings, labels.settings + "\tCtrl+,");
    set_text(game, kReload, labels.reload + "\tF5");
    set_text(game, kTitle, labels.title_return + "\tF10");
    EnableMenuItem(game, kTitle, MF_BYCOMMAND | (labels.title_enabled ? MF_ENABLED : MF_GRAYED));
    set_text(game, kExit, labels.exit + "\tAlt+F4");
    if (!installed || shape != last_shape) {
        // The first menu stays; the others are made again (DeleteMenu destroys their popups).
        while (GetMenuItemCount(bar) > 1) DeleteMenu(bar, 1, MF_BYPOSITION);
        ids.clear();
        for (const auto& menu : menus) {
            HMENU popup = CreatePopupMenu();
            fill(popup, menu.items);
            AppendMenuW(bar, MF_POPUP, UINT_PTR(popup), L"-");
        }
    }
    for (UINT n = 0; n < menus.size(); ++n) {
        set_text(bar, n + 1, bar_title(menus[n]), true);
        if (HMENU popup = GetSubMenu(bar, int(n + 1))) apply(popup, menus[n].items);
    }
    DrawMenuBar(hwnd);
    installed = true;
    last_labels = label_text;
    last_menus = all;
    last_shape = shape;
    model = menus;
}
bool available() { return installed; }
bool activate_settings() {
    if (!installed) return false;
    SendMessageW(hwnd, WM_COMMAND, kSettings, 0);
    return true;
}
bool activate(const std::string& title) {
    if (!installed) return false;
    const Item* wanted = nullptr;
    for (const auto& menu : model)
        if (!wanted) wanted = titled(menu.items, title);
    if (!wanted || !wanted->enabled) return false;
    for (size_t n = 0; n < ids.size(); ++n)
        if (ids[n] == wanted->id) {
            SendMessageW(hwnd, WM_COMMAND, kFirstEntry + UINT(n), 0);
            return true;
        }
    return false;
}
bool take_settings_request() { return requested.exchange(false); }
bool take_about_request() { return false; }   // Help's entries, by take_request()
bool take_update_request() { return false; }
bool take_reload_request() { return reload.exchange(false); }
std::string take_request() {
    if (chosen.empty()) return {};
    std::string id = std::move(chosen.front());
    chosen.pop_front();
    return id;
}
bool reveal(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) return false;
    const std::wstring arguments = L"/select,\"" + file.wstring() + L"\"";
    return INT_PTR(ShellExecuteW(hwnd, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
}
void shutdown() {
    // A destroyed window took its menu with it.
    if (hwnd && IsWindow(hwnd)) {
        if (WNDPROC(GetWindowLongPtrW(hwnd, GWLP_WNDPROC)) == &proc) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(sdl_proc));
        SetMenu(hwnd, nullptr);
        DestroyMenu(bar);  // with its popups
    }
    hwnd = nullptr; bar = game = nullptr; sdl_proc = nullptr;
    installed = false; bar_shown = false; requested = false; reload = false;
    chosen.clear(); ids.clear(); model.clear();
    last_labels.clear(); last_menus.clear(); last_shape.clear();
}
}
