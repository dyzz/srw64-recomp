#import <AppKit/AppKit.h>
#include "app_menu.hpp"
#include <atomic>

namespace {
std::atomic_bool requested{}, reload{}, fullscreen{}, installed{};
std::atomic_int scale{};
void on_main(dispatch_block_t work) {
    if ([NSThread isMainThread]) work();
    else dispatch_sync(dispatch_get_main_queue(), work);
}
}

@interface SRW64AppMenuTarget : NSObject
- (void)openSettings:(id)sender;
- (void)reloadDialogue:(id)sender;
- (void)toggleGameFullScreen:(id)sender;
- (void)scaleWindow:(id)sender;
@end
@implementation SRW64AppMenuTarget
- (void)openSettings:(id)sender { requested = true; }
- (void)reloadDialogue:(id)sender { reload = true; }
- (void)toggleGameFullScreen:(id)sender { fullscreen = true; }
- (void)scaleWindow:(id)sender { scale = int([sender tag]); }
@end

namespace srw64::app_menu {
namespace {
SRW64AppMenuTarget* target;
NSMenuItem* item;
NSMenuItem* reload_item;
NSMenuItem* separator;
NSMenuItem* view_item;  // the View menu's entry in the menu bar
NSMenuItem* fullscreen_item;
NSMenuItem* scale_items[kScales];
std::string last_title;
WindowState last_state{false, -1, -1};

NSString* text(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
std::string scale_title(const std::string& pattern, int n) {
    std::string title = pattern;
    if (const size_t at = title.find("{n}"); at != std::string::npos) title.replace(at, 3, std::to_string(n));
    return title;
}
}
void update(const Labels& labels, const WindowState& state) {
    const std::string title = labels.settings + "\n" + labels.reload + "\n" + labels.view + "\n" + labels.fullscreen + "\n" + labels.window_scale;
    if (installed && title == last_title && state.fullscreen == last_state.fullscreen &&
        state.scale == last_state.scale && state.largest == last_state.largest) return;
    on_main(^{
        if (NSApp.mainMenu.numberOfItems == 0) return;
        NSMenu* menu = [NSApp.mainMenu itemAtIndex:0].submenu;
        if (!menu) return; // SDL can create the application menu after the window.
        if (!item) {
            target = [SRW64AppMenuTarget new];
            item = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(openSettings:) keyEquivalent:@","];
            item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            item.target = target;
            reload_item = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(reloadDialogue:) keyEquivalent:@"r"];
            reload_item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            reload_item.target = target;
            separator = [NSMenuItem separatorItem];
            // After About and its separator, before Services/Hide/Quit.
            NSInteger index = MIN(2, menu.numberOfItems);
            [menu insertItem:item atIndex:index];
            [menu insertItem:reload_item atIndex:index + 1];
            [menu insertItem:separator atIndex:index + 2];

            NSMenu* view = [[NSMenu alloc] initWithTitle:@""];
            view.autoenablesItems = NO;
            fullscreen_item = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(toggleGameFullScreen:) keyEquivalent:@"f"];
            fullscreen_item.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
            fullscreen_item.target = target;
            [view addItem:fullscreen_item];
            [view addItem:[NSMenuItem separatorItem]];
            for (int n = 1; n <= kScales; ++n) {
                NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(scaleWindow:)
                                                        keyEquivalent:[NSString stringWithFormat:@"%d", n]];
                entry.keyEquivalentModifierMask = NSEventModifierFlagCommand;
                entry.target = target;
                entry.tag = n;
                [view addItem:entry];
                scale_items[n - 1] = entry;
            }
            view_item = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
            view_item.submenu = view;
            [NSApp.mainMenu insertItem:view_item atIndex:1];
            // SDL's Window menu has its own English Toggle Full Screen on the same keys.
            for (NSMenuItem* entry in NSApp.windowsMenu.itemArray)
                if (entry.action == @selector(toggleFullScreen:)) entry.hidden = YES;
        }
        item.title = text(labels.settings);
        reload_item.title = text(labels.reload);
        view_item.title = text(labels.view);
        view_item.submenu.title = text(labels.view);
        fullscreen_item.title = text(labels.fullscreen);
        fullscreen_item.state = state.fullscreen ? NSControlStateValueOn : NSControlStateValueOff;
        for (int n = 1; n <= kScales; ++n) {
            scale_items[n - 1].title = text(scale_title(labels.window_scale, n));
            scale_items[n - 1].state = state.scale == n ? NSControlStateValueOn : NSControlStateValueOff;
            scale_items[n - 1].enabled = !state.fullscreen && n <= state.largest;
        }
        installed = true;
    });
    if (installed) { last_title = title; last_state = state; }
}
bool available() { return installed; }
bool activate_settings() {
    __block bool activated = false;
    on_main(^{
        if (item.menu && item.enabled) {
            [item.menu performActionForItemAtIndex:[item.menu indexOfItem:item]];
            activated = true;
        }
    });
    return activated;
}
bool activate(const std::string& title) {
    NSString* wanted = text(title);
    __block bool activated = false;
    on_main(^{
        NSMenu* view = view_item.submenu;
        for (NSMenuItem* entry in view.itemArray)
            if ([entry.title isEqualToString:wanted] && entry.enabled && entry.action) {
                [view performActionForItemAtIndex:[view indexOfItem:entry]];
                activated = true;
                return;
            }
    });
    return activated;
}
bool take_settings_request() { return requested.exchange(false); }
bool take_reload_request() { return reload.exchange(false); }
bool take_fullscreen_request() { return fullscreen.exchange(false); }
int take_scale_request() { return scale.exchange(0); }
void shutdown() {
    on_main(^{
        [item.menu removeItem:item];
        [reload_item.menu removeItem:reload_item];
        [separator.menu removeItem:separator];
        [view_item.menu removeItem:view_item];
        item = nil; reload_item = nil; separator = nil; view_item = nil; fullscreen_item = nil;
        for (auto& entry : scale_items) entry = nil;
        target = nil;
    });
    installed = false; requested = false; reload = false; fullscreen = false; scale = 0;
    last_title.clear(); last_state = {false, -1, -1};
}
}
