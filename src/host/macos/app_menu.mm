#import <AppKit/AppKit.h>
#include "app_menu.hpp"
#include <atomic>

namespace {
std::atomic_bool requested{}, reload{}, installed{};
void on_main(dispatch_block_t work) {
    if ([NSThread isMainThread]) work();
    else dispatch_sync(dispatch_get_main_queue(), work);
}
}

@interface SRW64AppMenuTarget : NSObject
- (void)openSettings:(id)sender;
- (void)reloadDialogue:(id)sender;
@end
@implementation SRW64AppMenuTarget
- (void)openSettings:(id)sender { requested = true; }
- (void)reloadDialogue:(id)sender { reload = true; }
@end

namespace srw64::app_menu {
namespace {
SRW64AppMenuTarget* target;
NSMenuItem* item;
NSMenuItem* reload_item;
NSMenuItem* separator;
std::string last_title;
}
void update(const std::string& settings_title,const std::string& reload_title) {
    const std::string title = settings_title + "\n" + reload_title;
    if (installed && title == last_title) return;
    NSString* label = [NSString stringWithUTF8String:settings_title.c_str()];
    NSString* reload_label = [NSString stringWithUTF8String:reload_title.c_str()];
    on_main(^{
        if (NSApp.mainMenu.numberOfItems == 0) return;
        NSMenu* menu = [NSApp.mainMenu itemAtIndex:0].submenu;
        if (!menu) return; // SDL can create the application menu after the window.
        if (!item) {
            target = [SRW64AppMenuTarget new];
            item = [[NSMenuItem alloc] initWithTitle:label action:@selector(openSettings:) keyEquivalent:@","];
            item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            item.target = target;
            reload_item = [[NSMenuItem alloc] initWithTitle:reload_label action:@selector(reloadDialogue:) keyEquivalent:@"r"];
            reload_item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            reload_item.target = target;
            separator = [NSMenuItem separatorItem];
            // After About and its separator, before Services/Hide/Quit.
            NSInteger index = MIN(2, menu.numberOfItems);
            [menu insertItem:item atIndex:index];
            [menu insertItem:reload_item atIndex:index + 1];
            [menu insertItem:separator atIndex:index + 2];
        }
        item.title = label;
        reload_item.title = reload_label;
        installed = true;
    });
    if (installed) last_title = title;
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
bool take_settings_request() { return requested.exchange(false); }
bool take_reload_request() { return reload.exchange(false); }
void shutdown() {
    on_main(^{
        [item.menu removeItem:item];
        [reload_item.menu removeItem:reload_item];
        [separator.menu removeItem:separator];
        item = nil; reload_item = nil; separator = nil; target = nil;
    });
    installed = false; requested = false; reload = false; last_title.clear();
}
}
