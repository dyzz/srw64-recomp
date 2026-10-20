#import <AppKit/AppKit.h>
#include "app_menu.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <deque>
#include <mutex>

namespace {
std::atomic_bool requested{}, reload{}, installed{}, about{}, updates{};
std::mutex chosen_mutex;
std::deque<std::string> chosen;  // ids of entries chosen, for take_request()
void on_main(dispatch_block_t work) {
    if ([NSThread isMainThread]) work();
    else dispatch_sync(dispatch_get_main_queue(), work);
}
}

@interface SRW64AppMenuTarget : NSObject
- (void)openSettings:(id)sender;
- (void)reloadDialogue:(id)sender;
- (void)openAbout:(id)sender;
- (void)checkForUpdates:(id)sender;
- (void)chooseEntry:(id)sender;
@end
@implementation SRW64AppMenuTarget
- (void)openSettings:(id)sender { requested = true; }
- (void)reloadDialogue:(id)sender { reload = true; }
- (void)openAbout:(id)sender { about = true; }
- (void)checkForUpdates:(id)sender { updates = true; }
- (void)chooseEntry:(NSMenuItem*)sender {
    NSString* entry = sender.representedObject;
    if (!entry) return;
    std::lock_guard lock(chosen_mutex);
    chosen.emplace_back(entry.UTF8String);
}
@end

namespace srw64::app_menu {
namespace {
SRW64AppMenuTarget* target;
NSMenuItem* item;
NSMenuItem* reload_item;
NSMenuItem* separator;
NSMenuItem* about_item;    // SDL's About entry, turned to the settings window's About page
NSMenuItem* updates_item;
NSMutableArray<NSMenuItem*>* bar_items;  // the menus update() describes, in the menu bar after the application menu
std::string last_labels, last_menus, last_shape;
std::vector<Menu> model;  // as last shown, for activate()

NSString* text(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
// "F1".."F19" are the function keys; anything else is the key's own character.
NSString* key_equivalent(const std::string& key) {
    if (key.size() >= 2 && key[0] == 'F' && std::isdigit(static_cast<unsigned char>(key[1]))) {
        const unichar f = unichar(NSF1FunctionKey + std::stoi(key.substr(1)) - 1);
        return [NSString stringWithCharacters:&f length:1];
    }
    return text(key);
}
NSEventModifierFlags modifiers(unsigned mods) {
    NSEventModifierFlags flags = 0;
    if (mods & Command) flags |= NSEventModifierFlagCommand;
    if (mods & Control) flags |= NSEventModifierFlagControl;
    return flags;
}
// A key that is only shown (a bound one; as a key equivalent the menu would take it from
// the game on every screen) sits right-aligned in one column per menu, where AppKit puts
// key equivalents. No colours: AppKit then draws the disabled and highlighted states.
bool shown_key(const Item& i) { return !i.shortcut.empty() && i.mac_key.empty(); }
CGFloat key_column(const std::vector<Item>& items) {
    NSFont* font = [NSFont menuFontOfSize:0];
    CGFloat widest = 0;
    for (const auto& i : items)
        if (shown_key(i)) widest = std::max(widest, [text(i.title) sizeWithAttributes:@{NSFontAttributeName: font}].width);
    return widest + 56;
}
NSAttributedString* with_key(const std::string& title, const std::string& key, CGFloat column) {
    NSMutableParagraphStyle* style = [NSMutableParagraphStyle new];
    style.tabStops = @[[[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentRight location:column options:@{}]];
    return [[NSAttributedString alloc] initWithString:text(title + "\t" + key)
                                           attributes:@{NSFontAttributeName: [NSFont menuFontOfSize:0], NSParagraphStyleAttributeName: style}];
}
// A bar entry with an icon: the system's symbol and the title drawn as one template image
// (the bar shows an entry's image alone, without its title, and ignores attributed titles),
// semibold to sit near the bar's own titles; the title stays the entry's accessible name.
NSImage* icon_with_title(const std::string& icon, const std::string& title) {
    NSFont* font = [NSFont systemFontOfSize:[NSFont menuBarFontOfSize:0].pointSize weight:NSFontWeightSemibold];
    NSImage* symbol = [[NSImage imageWithSystemSymbolName:text(icon) accessibilityDescription:nil]
        imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:font.pointSize weight:NSFontWeightSemibold]];
    NSDictionary* attributes = @{NSFontAttributeName: font, NSForegroundColorAttributeName: NSColor.blackColor};
    NSString* words = text(" " + title);
    const NSSize room = [words sizeWithAttributes:attributes];
    const CGFloat height = std::ceil(std::max(room.height, symbol.size.height));
    NSImage* image = [NSImage imageWithSize:NSMakeSize(std::ceil(symbol.size.width + room.width), height) flipped:NO
                             drawingHandler:^BOOL(NSRect) {
        [symbol drawInRect:NSMakeRect(0, (height - symbol.size.height) / 2, symbol.size.width, symbol.size.height)];
        [words drawAtPoint:NSMakePoint(symbol.size.width, (height - room.height) / 2) withAttributes:attributes];
        return YES;
    }];
    [image setTemplate:YES];
    return image;
}
// What the entries say and whether they are on (last_menus), and their ids and keys alone
// (last_shape): a change of shape builds the menus again, any other is set in place.
void describe(const std::vector<Item>& items, std::string& all, std::string& shape) {
    for (const auto& i : items) {
        shape += i.id + "\x1f" + i.mac_key + "\x1f" + std::to_string(i.mac_mods) + "{";
        all += i.id + "\x1f" + i.title + "\x1f" + i.shortcut + "\x1f" + i.mac_key + (i.checked ? "+" : "-") + (i.enabled ? "+" : "-") + "{";
        describe(i.items, all, shape);
        shape += "}"; all += "}";
    }
}
void fill(NSMenu* menu, const std::vector<Item>& items) {
    menu.autoenablesItems = NO;
    for (const auto& i : items) {
        if (i.id.empty()) { [menu addItem:[NSMenuItem separatorItem]]; continue; }
        NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(chooseEntry:) keyEquivalent:@""];
        entry.target = target;
        entry.representedObject = text(i.id);
        if (!i.mac_key.empty()) {
            entry.keyEquivalent = key_equivalent(i.mac_key);
            entry.keyEquivalentModifierMask = modifiers(i.mac_mods);
        }
        if (!i.items.empty()) {
            entry.action = nil;
            entry.submenu = [[NSMenu alloc] initWithTitle:@""];
            fill(entry.submenu, i.items);
        }
        [menu addItem:entry];
    }
}
void apply(NSMenu* menu, const std::vector<Item>& items) {
    const CGFloat column = key_column(items);
    for (NSInteger n = 0; n < NSInteger(items.size()) && n < menu.numberOfItems; ++n) {
        const Item& i = items[size_t(n)];
        NSMenuItem* entry = [menu itemAtIndex:n];
        if (i.id.empty()) continue;
        if (shown_key(i)) entry.attributedTitle = with_key(i.title, i.shortcut, column);
        else {
            entry.attributedTitle = nil;
            entry.title = text(i.title);
        }
        entry.state = i.checked ? NSControlStateValueOn : NSControlStateValueOff;
        entry.enabled = i.enabled;
        if (entry.submenu) {
            entry.submenu.title = text(i.title);
            apply(entry.submenu, i.items);
        }
    }
}
NSMenuItem* find(NSMenu* menu, NSString* wanted) {
    for (NSMenuItem* entry in menu.itemArray) {
        if ([entry.representedObject isEqual:wanted]) return entry;
        if (entry.submenu)
            if (NSMenuItem* inner = find(entry.submenu, wanted)) return inner;
    }
    return nil;
}
const Item* titled(const std::vector<Item>& items, const std::string& title) {
    for (const auto& i : items) {
        if (!i.id.empty() && i.items.empty() && i.title == title) return &i;
        if (const Item* inner = titled(i.items, title)) return inner;
    }
    return nullptr;
}
}
void update(const Labels& labels, const std::vector<Menu>& menus, bool) {
    const std::string label_text = labels.settings + "\n" + labels.reload + "\n" + labels.about + "\n" + labels.check_updates;
    std::string all, shape;
    for (const auto& menu : menus) {
        all += menu.title + "\x1f" + menu.icon + "[";
        shape += "[";
        describe(menu.items, all, shape);
    }
    if (installed && label_text == last_labels && all == last_menus) return;
    const bool rebuild = !installed || shape != last_shape;
    const std::vector<Menu> wanted = menus;
    __block bool done = false;
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
            // SDL's About shows the system panel; ours has the links and the update check.
            for (NSMenuItem* entry in menu.itemArray)
                if (entry.action == @selector(orderFrontStandardAboutPanel:)) about_item = entry;
            updates_item = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(checkForUpdates:) keyEquivalent:@""];
            updates_item.target = target;
            NSInteger after_about = about_item ? [menu indexOfItem:about_item] + 1 : 0;
            [menu insertItem:updates_item atIndex:after_about];
            if (about_item) {
                about_item.target = target;
                about_item.action = @selector(openAbout:);
            }
            // After About, Check for Updates and their separator, before Services/Hide/Quit.
            NSInteger index = MIN(after_about + 2, menu.numberOfItems);
            [menu insertItem:item atIndex:index];
            [menu insertItem:reload_item atIndex:index + 1];
            [menu insertItem:separator atIndex:index + 2];
            // SDL's Window menu has its own English Toggle Full Screen on View's keys.
            for (NSMenuItem* entry in NSApp.windowsMenu.itemArray)
                if (entry.action == @selector(toggleFullScreen:)) entry.hidden = YES;
        }
        item.title = text(labels.settings);
        if (about_item) about_item.title = text(labels.about);
        updates_item.title = text(labels.check_updates);
        reload_item.title = text(labels.reload);
        if (rebuild) {
            for (NSMenuItem* old in bar_items) [old.menu removeItem:old];
            bar_items = [NSMutableArray new];
            for (size_t n = 0; n < wanted.size(); ++n) {
                NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
                top.submenu = [[NSMenu alloc] initWithTitle:@""];
                fill(top.submenu, wanted[n].items);
                [NSApp.mainMenu insertItem:top atIndex:NSInteger(n) + 1];
                [bar_items addObject:top];
            }
        }
        for (size_t n = 0; n < wanted.size() && n < bar_items.count; ++n) {
            bar_items[n].title = text(wanted[n].title);
            bar_items[n].image = wanted[n].icon.empty() ? nil : icon_with_title(wanted[n].icon, wanted[n].title);
            bar_items[n].submenu.title = text(wanted[n].title);
            apply(bar_items[n].submenu, wanted[n].items);
        }
        installed = true;
        done = true;
    });
    if (done) {
        last_labels = label_text;
        last_menus = all;
        last_shape = shape;
        model = menus;
    }
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
    const Item* wanted = nullptr;
    for (const auto& menu : model)
        if (!wanted) wanted = titled(menu.items, title);
    if (!wanted || !wanted->enabled) return false;
    NSString* entry_id = text(wanted->id);
    __block bool activated = false;
    on_main(^{
        for (NSMenuItem* top in bar_items) {
            NSMenuItem* entry = find(top.submenu, entry_id);
            if (!entry || !entry.enabled) continue;
            [entry.menu performActionForItemAtIndex:[entry.menu indexOfItem:entry]];
            activated = true;
            return;
        }
    });
    return activated;
}
bool take_settings_request() { return requested.exchange(false); }
bool take_about_request() { return about.exchange(false); }
bool take_update_request() { return updates.exchange(false); }
bool take_reload_request() { return reload.exchange(false); }
std::string take_request() {
    std::lock_guard lock(chosen_mutex);
    if (chosen.empty()) return {};
    std::string id = std::move(chosen.front());
    chosen.pop_front();
    return id;
}
bool reveal(const std::filesystem::path& file) {
    NSURL* url = [NSURL fileURLWithPath:text(file.string())];
    if (![url checkResourceIsReachableAndReturnError:nil]) return false;
    on_main(^{ [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[url]]; });
    return true;
}
void shutdown() {
    on_main(^{
        [item.menu removeItem:item];
        [reload_item.menu removeItem:reload_item];
        [separator.menu removeItem:separator];
        for (NSMenuItem* top in bar_items) [top.menu removeItem:top];
        [updates_item.menu removeItem:updates_item];
        if (about_item) {
            about_item.target = nil;
            about_item.action = @selector(orderFrontStandardAboutPanel:);
        }
        about_item = nil; updates_item = nil;
        item = nil; reload_item = nil; separator = nil; bar_items = nil;
        target = nil;
    });
    installed = false; requested = false; reload = false; about = false; updates = false;
    { std::lock_guard lock(chosen_mutex); chosen.clear(); }
    last_labels.clear(); last_menus.clear(); last_shape.clear(); model.clear();
}
}
