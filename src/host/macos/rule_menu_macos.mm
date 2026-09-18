#import <Cocoa/Cocoa.h>
#include "rule_menu.hpp"
#include "rule_fixes.hpp"
#include "settings_window.hpp"
#include "localization/catalog.hpp"
#include <atomic>
#include <cstdio>
#include <exception>
#include <string>

// 选项 in the menu bar: the 游戏性调整 submenu with one checkbox per optional rule
// (rule_fixes.hpp) and the presets, and the item that opens the settings window.
// A change takes effect at the game's next calculation and is written back to the
// launcher's settings, so the next launch keeps it. The game's own menus are untouched.

namespace srw64::rule_menu {
namespace {
// An unknown key resolves to itself, so a rule added later still appears, with
// its ui key as the title.
NSString* label(const std::string& key) {
    return [NSString stringWithUTF8String:localization::catalog().ui(key).c_str()];
}
}
}

@interface SRW64RuleMenu : NSObject
@property(nonatomic,strong) NSMenuItem* item;            // "选项" in the menu bar
@property(nonatomic,strong) NSMenu* gameplay;            // its "游戏性调整" submenu
@property(nonatomic,strong) NSMutableArray<NSMenuItem*>* rules;
// Every titled item with the ui key it was built from: one retitle path for the
// rule items, the group headers, the presets and the note.
@property(nonatomic,strong) NSMutableArray<NSMenuItem*>* titled;
@property(nonatomic,strong) NSMutableArray<NSString*>* titleKeys;
- (void)toggle:(NSMenuItem*)sender;
- (void)openSettings:(NSMenuItem*)sender;
- (void)refresh;
- (void)retitle;
- (void)remember:(NSMenuItem*)item key:(const std::string&)key;
@end

@implementation SRW64RuleMenu
- (void)refresh {
    const unsigned fixes=srw64::rules::active_fixes();
    for(NSMenuItem* item in self.rules)
        item.state=(fixes&[(NSNumber*)item.representedObject unsignedIntValue])?NSControlStateValueOn:NSControlStateValueOff;
}
- (void)remember:(NSMenuItem*)item key:(const std::string&)key {
    [self.titled addObject:item];
    [self.titleKeys addObject:[NSString stringWithUTF8String:key.c_str()]];
}
// Main thread. Titles follow the language the player picked with F7.
- (void)retitle {
    using namespace srw64::rule_menu;
    for(NSUInteger index=0;index<self.titled.count && index<self.titleKeys.count;++index)
        self.titled[index].title=label(self.titleKeys[index].UTF8String);
    self.gameplay.title=label("rules_menu");
    self.item.submenu.title=label("options_menu");
}
- (void)openSettings:(NSMenuItem*)sender {
    srw64::settings_window::open();
}
// Main thread. A preset item carries the whole set; a rule item flips its bit.
- (void)toggle:(NSMenuItem*)sender {
    using namespace srw64;
    const unsigned value=[(NSNumber*)sender.representedObject unsignedIntValue];
    const bool preset=sender.tag==1;
    const unsigned current=rules::active_fixes();
    const unsigned next=preset?value:(sender.state==NSControlStateValueOn?current&~value:current|value);
    try {
        rules::set_fixes(next);
    } catch(const std::exception& error) {
        NSLog(@"SRW64_RULE_MENU %s",error.what());
    }
    [self refresh];
}
@end

namespace srw64::rule_menu {
namespace {
SRW64RuleMenu* controller;
std::string built_locale;   // Main thread: the language the titles were built in.
}

// Window thread, every frame: installs the menu once the menu bar exists (SDL may
// build it after the window appears) and re-titles it after a language switch.
void update() {
    static std::atomic_bool installed{};
    if(installed.load(std::memory_order_relaxed)) {
        const std::string locale=localization::catalog().locale;
        dispatch_async(dispatch_get_main_queue(),^{
            if(!controller || built_locale==locale)return;
            built_locale=locale;
            [controller retitle];
        });
        return;
    }
    dispatch_async(dispatch_get_main_queue(),^{
        if(controller)return;
        if(!NSApp.mainMenu) {
            // SDL builds the menu bar when the app becomes a foreground app, which
            // can be after the window exists; the caller retries every frame.
            static bool reported=false;
            if(!reported){reported=true;std::fprintf(stderr,"SRW64_RULE_MENU waiting for the menu bar\n");}
            return;
        }
        controller=[SRW64RuleMenu new];
        controller.rules=[NSMutableArray array];
        controller.titled=[NSMutableArray array];
        controller.titleKeys=[NSMutableArray array];
        // 选项 → 游戏性调整, with the corrections and the difficulty options in
        // separate groups under their own disabled headers.
        NSMenu* gameplay=[[NSMenu alloc] initWithTitle:label("rules_menu")];
        gameplay.autoenablesItems=NO;
        auto header=[&](const std::string& key) {
            NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:label(key) action:nil keyEquivalent:@""];
            item.enabled=NO;
            [gameplay addItem:item];
            [controller remember:item key:key];
        };
        auto rule_items=[&](unsigned mask) {
            for(const auto& entry:rules::catalog) {
                if(!(entry.fix&mask))continue;
                const auto key=rules::ui_key(entry.id);
                NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:label(key)
                                                            action:@selector(toggle:) keyEquivalent:@""];
                item.target=controller;
                item.representedObject=@(unsigned(entry.fix));
                item.indentationLevel=1;
                [gameplay addItem:item];
                [controller.rules addObject:item];
                [controller remember:item key:key];
            }
        };
        header("rules_group_corrections");
        rule_items(rules::corrections);
        [gameplay addItem:[NSMenuItem separatorItem]];
        header("rules_group_difficulty");
        rule_items(rules::difficulty);
        [gameplay addItem:[NSMenuItem separatorItem]];
        // Tag 1 marks a preset: its action applies the whole set instead of one bit.
        for(const auto& preset:rules::presets) {
            const std::string key(preset.key);
            NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:label(key)
                                                        action:@selector(toggle:) keyEquivalent:@""];
            item.target=controller;
            item.representedObject=@(preset.fixes);
            item.tag=1;
            [gameplay addItem:item];
            [controller remember:item key:key];
        }
        [gameplay addItem:[NSMenuItem separatorItem]];
        NSMenuItem* note=[[NSMenuItem alloc] initWithTitle:label("rules_note") action:nil keyEquivalent:@""];
        note.enabled=NO;
        [gameplay addItem:note];
        [controller remember:note key:"rules_note"];
        controller.gameplay=gameplay;

        NSMenu* options=[[NSMenu alloc] initWithTitle:label("options_menu")];
        options.autoenablesItems=NO;
        NSMenuItem* gameplay_item=[[NSMenuItem alloc] initWithTitle:label("rules_menu") action:nil keyEquivalent:@""];
        gameplay_item.submenu=gameplay;
        [options addItem:gameplay_item];
        [controller remember:gameplay_item key:"rules_menu"];
        [options addItem:[NSMenuItem separatorItem]];
        NSMenuItem* settings=[[NSMenuItem alloc] initWithTitle:label("settings_open")
                                                       action:@selector(openSettings:) keyEquivalent:@","];
        settings.target=controller;
        [options addItem:settings];
        [controller remember:settings key:"settings_open"];

        controller.item=[[NSMenuItem alloc] initWithTitle:label("options_menu") action:nil keyEquivalent:@""];
        controller.item.submenu=options;
        [NSApp.mainMenu addItem:controller.item];
        [controller refresh];
        built_locale=localization::catalog().locale;
        installed.store(true,std::memory_order_relaxed);
        std::fprintf(stderr,"SRW64_RULE_MENU installed rules=%ld\n",(long)controller.rules.count);
    });
}

void shutdown() {
    dispatch_async(dispatch_get_main_queue(),^{
        if(controller.item && NSApp.mainMenu)[NSApp.mainMenu removeItem:controller.item];
        controller=nil;
    });
}

// Window thread, QA only. The item is pressed through its own menu, so the test
// covers the item's target, action and represented rule, not just set_fixes.
void control(const std::filesystem::path& directory) {
    if(!std::getenv("SRW64_WINDOW_CONTROL"))return;
    static uint64_t sequence{};
    std::ifstream file(directory/"rule-control.json");
    const auto command=nlohmann::json::parse(file,nullptr,false);
    if(!command.is_object() || command.value("schema","")!="srw64.rule-control.v1" ||
       command.value("sequence",uint64_t{})<=sequence)return;
    sequence=command.at("sequence");
    const auto wanted=command.value("item",std::string{});
    const auto path=directory;
    // The window thread is the main thread here, where dispatch_sync would
    // deadlock; run the block directly and only dispatch from another thread.
    void (^press)(void)=^{
        NSMenu* menu=controller.gameplay;
        nlohmann::json row={{"schema","srw64.rule-menu-event.v1"},{"sequence",sequence},{"item",wanted},
                            {"vi",srw64_current_vi()},{"pressed",false}};
        NSString* title=nil;
        if(wanted=="original")title=label("rules_original");
        else if(wanted=="all")title=label("rules_all");
        else if(wanted=="defaults")title=label("rules_defaults");
        else for(const auto& entry:rules::catalog)if(entry.id==wanted)title=label(rules::ui_key(entry.id));
        for(NSInteger index=0;menu && title && index<menu.numberOfItems;++index) {
            if(![[menu itemAtIndex:index].title isEqualToString:title])continue;
            [menu performActionForItemAtIndex:index];
            row["pressed"]=true;
            break;
        }
        nlohmann::json items=nlohmann::json::array();
        for(NSInteger index=0;menu && index<menu.numberOfItems;++index) {
            NSMenuItem* item=[menu itemAtIndex:index];
            if(item.isSeparatorItem)continue;
            items.push_back({{"title",item.title.UTF8String},{"checked",item.state==NSControlStateValueOn},
                             {"preset",item.tag==1},{"enabled",item.isEnabled?true:false}});
        }
        row["items"]=items;
        row["enabled"]=rules::names(rules::active_fixes());
        std::ofstream(path/"rule-menu-events.jsonl",std::ios::app)<<row.dump()<<'\n';
    };
    if(NSThread.isMainThread)press();
    else dispatch_sync(dispatch_get_main_queue(),press);
}
}
