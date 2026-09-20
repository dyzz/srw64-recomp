#import <AppKit/AppKit.h>
#include "app_menu.hpp"
#include <atomic>

namespace {
std::atomic_bool requested{}, installed{};
void on_main(dispatch_block_t work) {
    if ([NSThread isMainThread]) work();
    else dispatch_sync(dispatch_get_main_queue(), work);
}
}

@interface SRW64AppMenuTarget : NSObject
- (void)openSettings:(id)sender;
@end
@implementation SRW64AppMenuTarget
- (void)openSettings:(id)sender { requested = true; }
@end

namespace srw64::app_menu {
namespace {
SRW64AppMenuTarget* target;
NSMenuItem* item;
NSMenuItem* separator;
std::string last_title;
}
void update(const std::string& title) {
    if (installed && title == last_title) return;
    NSString* label = [NSString stringWithUTF8String:title.c_str()];
    on_main(^{
        if (NSApp.mainMenu.numberOfItems == 0) return;
        NSMenu* menu = [NSApp.mainMenu itemAtIndex:0].submenu;
        if (!menu) return; // SDL can create the application menu after the window.
        if (!item) {
            target = [SRW64AppMenuTarget new];
            item = [[NSMenuItem alloc] initWithTitle:label action:@selector(openSettings:) keyEquivalent:@","];
            item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            item.target = target;
            separator = [NSMenuItem separatorItem];
            // After About and its separator, before Services/Hide/Quit.
            NSInteger index = MIN(2, menu.numberOfItems);
            [menu insertItem:item atIndex:index];
            [menu insertItem:separator atIndex:index + 1];
        }
        item.title = label;
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
void shutdown() {
    on_main(^{
        [item.menu removeItem:item];
        [separator.menu removeItem:separator];
        item = nil; separator = nil; target = nil;
    });
    installed = false; requested = false; last_title.clear();
}
}
