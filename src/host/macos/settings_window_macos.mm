#import <Cocoa/Cocoa.h>
#include "settings_window.hpp"
#include "rule_fixes.hpp"
#include "modal_input.hpp"
#include "presentation_settings.hpp"
#include "presentation/image_mode.hpp"
#include "localization/catalog.hpp"
#include <atomic>
#include <string>
#include <vector>

// One panel for everything a player can change while the game runs: the optional
// rules (same switches as 选项 → 游戏性调整), the language and the image mode.
// Every control applies at once through the same paths the menu and the hotkeys
// use; nothing here writes game memory or touches the renderer.
namespace srw64::settings_window {
namespace {
std::atomic_bool shown{};
ModalInputRelease release_gate;

NSString* text(const std::string& key) {
    return [NSString stringWithUTF8String:localization::catalog().ui(key).c_str()];
}
}
}

// A control plus what it stands for: rules carry their bit, the language buttons
// their locale, the image buttons their mode. The panel is rebuilt from these on
// a language switch, so no title is stored twice.
@interface SRW64Setting : NSObject
@property(nonatomic,strong) NSButton* button;
@property(nonatomic,copy) NSString* labelKey;
@property(nonatomic,copy) NSString* identifierText;   // "rule:esp-level", "locale:ja", "images:hd", "preset:rules_defaults"
@property(nonatomic) unsigned bits;
@end
@implementation SRW64Setting
@end

@interface SRW64SettingsWindow : NSObject <NSWindowDelegate>
@property(nonatomic,strong) NSPanel* panel;
@property(nonatomic,strong) NSMutableArray<SRW64Setting*>* settings;
@property(nonatomic,strong) NSMutableArray<NSTextField*>* labels;
@property(nonatomic,strong) NSMutableArray<NSString*>* labelKeys;
@property(nonatomic,strong) NSStackView* presets;
- (void)build;
- (void)refresh;
- (void)retitle;
- (void)fit;
- (void)activate:(NSButton*)sender;
@end

@implementation SRW64SettingsWindow

- (void)remember:(NSTextField*)field key:(const char*)key {
    [self.labels addObject:field];
    [self.labelKeys addObject:[NSString stringWithUTF8String:key]];
}

- (NSTextField*)heading:(const char*)key size:(CGFloat)size {
    using namespace srw64::settings_window;
    NSTextField* field=[NSTextField labelWithString:text(key)];
    field.font=[NSFont boldSystemFontOfSize:size];
    [self remember:field key:key];
    return field;
}

- (NSTextField*)note:(const char*)key {
    using namespace srw64::settings_window;
    NSTextField* field=[NSTextField labelWithString:text(key)];
    field.font=[NSFont systemFontOfSize:10];
    field.textColor=NSColor.secondaryLabelColor;
    [self remember:field key:key];
    return field;
}

- (NSButton*)choice:(const std::string&)key identifier:(const char*)identifier bits:(unsigned)bits radio:(BOOL)radio {
    using namespace srw64::settings_window;
    NSButton* button=radio?[NSButton radioButtonWithTitle:text(key) target:self action:@selector(activate:)]
                          :[NSButton checkboxWithTitle:text(key) target:self action:@selector(activate:)];
    SRW64Setting* setting=[SRW64Setting new];
    setting.button=button;
    setting.labelKey=[NSString stringWithUTF8String:key.c_str()];
    setting.identifierText=[NSString stringWithUTF8String:identifier];
    setting.bits=bits;
    [self.settings addObject:setting];
    return button;
}

- (void)build {
    using namespace srw64;
    using namespace srw64::settings_window;
    self.settings=[NSMutableArray array];
    self.labels=[NSMutableArray array];
    self.labelKeys=[NSMutableArray array];
    NSStackView* stack=[[NSStackView alloc] initWithFrame:NSMakeRect(0,0,460,10)];
    stack.orientation=NSUserInterfaceLayoutOrientationVertical;
    stack.alignment=NSLayoutAttributeLeading;
    stack.spacing=6;
    stack.edgeInsets=NSEdgeInsetsMake(18,20,18,20);

    [stack addArrangedSubview:[self heading:"rules_menu" size:14]];
    auto group=[&](const char* heading,unsigned mask) {
        [stack addArrangedSubview:[self heading:heading size:11]];
        for(const auto& entry:rules::catalog)
            if(entry.fix&mask)
                [stack addArrangedSubview:[self choice:rules::ui_key(entry.id)
                                            identifier:("rule:"+std::string(entry.id)).c_str()
                                                  bits:entry.fix radio:NO]];
    };
    group("rules_group_corrections",rules::corrections);
    group("rules_group_difficulty",rules::difficulty);
    [stack addArrangedSubview:[self note:"rules_note"]];

    NSStackView* presets=[[NSStackView alloc] init];
    presets.orientation=NSUserInterfaceLayoutOrientationHorizontal;
    presets.spacing=8;
    for(const auto& preset:rules::presets) {
        const std::string key(preset.key);
        NSButton* button=[NSButton buttonWithTitle:text(key) target:self action:@selector(activate:)];
        SRW64Setting* setting=[SRW64Setting new];
        setting.button=button;
        setting.labelKey=[NSString stringWithUTF8String:key.c_str()];
        setting.identifierText=[NSString stringWithFormat:@"preset:%s",key.c_str()];
        setting.bits=preset.fixes;
        [self.settings addObject:setting];
        [presets addArrangedSubview:button];
    }
    [stack addArrangedSubview:presets];
    self.presets=presets;

    [stack addArrangedSubview:[self heading:"settings_language" size:14]];
    NSStackView* languages=[[NSStackView alloc] init];
    languages.orientation=NSUserInterfaceLayoutOrientationVertical;
    languages.alignment=NSLayoutAttributeLeading;
    languages.spacing=2;
    for(const auto& [locale,snapshot]:localization::registered()) {
        NSButton* button=[NSButton radioButtonWithTitle:
            [NSString stringWithUTF8String:localization::display_name(locale).c_str()]
            target:self action:@selector(activate:)];
        SRW64Setting* setting=[SRW64Setting new];
        setting.button=button;
        setting.identifierText=[NSString stringWithFormat:@"locale:%s",locale.c_str()];
        [self.settings addObject:setting];
        [languages addArrangedSubview:button];
    }
    [stack addArrangedSubview:languages];
    [stack addArrangedSubview:[self note:"settings_language_note"]];

    [stack addArrangedSubview:[self heading:"settings_images" size:14]];
    NSStackView* images=[[NSStackView alloc] init];
    images.orientation=NSUserInterfaceLayoutOrientationVertical;
    images.alignment=NSLayoutAttributeLeading;
    images.spacing=2;
    [images addArrangedSubview:[self choice:"settings_images_original" identifier:"images:original" bits:0 radio:YES]];
    [images addArrangedSubview:[self choice:"settings_images_hd" identifier:"images:hd" bits:1 radio:YES]];
    [stack addArrangedSubview:images];
    [stack addArrangedSubview:[self note:"settings_images_note"]];

    const NSRect frame=NSMakeRect(0,0,460,10);
    stack.frame=frame;
    self.panel=[[NSPanel alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskUtilityWindow
        backing:NSBackingStoreBuffered defer:NO];
    self.panel.title=text("settings_title");
    self.panel.contentView=stack;
    self.panel.delegate=self;
    self.panel.releasedWhenClosed=NO;
    self.panel.hidesOnDeactivate=NO;
    [self fit];
    [self.panel center];
    [self refresh];
}

// At least 460 points wide, and as wide as the longest row (the preset buttons
// in English) so nothing is clipped; again after a language change.
- (void)fit {
    NSStackView* content=(NSStackView*)self.panel.contentView;
    const NSSize size=content.fittingSize;
    // The vertical stack's fitting width leaves the horizontal preset row's
    // trailing inset out, so measure that row with both insets.
    const CGFloat row=self.presets.fittingSize.width+content.edgeInsets.left+content.edgeInsets.right;
    [self.panel setContentSize:NSMakeSize(ceil(MAX(460,MAX(size.width,row))),ceil(size.height))];
}

// Values follow the game, not the other way round: read them back every frame so
// a change made in the menu, by F6/F7 or by the QA hooks shows up here too.
- (void)refresh {
    using namespace srw64;
    const unsigned fixes=rules::active_fixes();
    const std::string locale=localization::catalog().locale;
    const bool hd_available=presentation::image_mode.enabled();
    const bool hd=presentation::image_mode.requested();
    for(SRW64Setting* setting in self.settings) {
        const std::string id=setting.identifierText.UTF8String;
        if(id.rfind("rule:",0)==0)
            setting.button.state=(fixes&setting.bits)?NSControlStateValueOn:NSControlStateValueOff;
        else if(id.rfind("locale:",0)==0)
            setting.button.state=(id.substr(7)==locale)?NSControlStateValueOn:NSControlStateValueOff;
        else if(id.rfind("images:",0)==0) {
            setting.button.enabled=hd_available;
            setting.button.state=((id=="images:hd")==hd)?NSControlStateValueOn:NSControlStateValueOff;
        }
    }
}

- (void)retitle {
    using namespace srw64::settings_window;
    for(NSUInteger index=0;index<self.labels.count && index<self.labelKeys.count;++index)
        self.labels[index].stringValue=text(self.labelKeys[index].UTF8String);
    for(SRW64Setting* setting in self.settings) {
        const std::string id=setting.identifierText.UTF8String;
        if(id.rfind("locale:",0)==0)
            setting.button.title=[NSString stringWithUTF8String:srw64::localization::display_name(id.substr(7)).c_str()];
        else if(setting.labelKey)
            setting.button.title=text(setting.labelKey.UTF8String);
    }
    self.panel.title=text("settings_title");
    [self fit];
}

// Main thread. Each control applies through the same path as the menu or hotkey.
- (void)activate:(NSButton*)sender {
    using namespace srw64;
    SRW64Setting* found=nil;
    for(SRW64Setting* setting in self.settings)if(setting.button==sender)found=setting;
    if(!found)return;
    const std::string id=found.identifierText.UTF8String;
    try {
        if(id.rfind("rule:",0)==0) {
            const unsigned current=rules::active_fixes();
            rules::set_fixes(sender.state==NSControlStateValueOn?current|found.bits:current&~found.bits);
        } else if(id.rfind("preset:",0)==0) {
            rules::set_fixes(found.bits);
        } else if(id.rfind("locale:",0)==0) {
            settings::request_locale(id.substr(7));
        } else if(id.rfind("images:",0)==0) {
            presentation::image_mode.request(id=="images:hd");
        }
    } catch(const std::exception& error) {
        NSLog(@"SRW64_SETTINGS %s",error.what());
    }
    [self refresh];
}

- (void)windowWillClose:(NSNotification*)notification {
    srw64::settings_window::close();
}
@end

namespace srw64::settings_window {
namespace {
SRW64SettingsWindow* window;
std::string built_locale;
}

namespace {
void open_now() {
    if(!window) {
        window=[SRW64SettingsWindow new];
        [window build];
        built_locale=localization::catalog().locale;
    }
    [window refresh];
    [window.panel makeKeyAndOrderFront:nil];
    shown=true;
    release_gate.hold();
    std::fprintf(stderr,"SRW64_SETTINGS opened\n");
}
void close_now() {
    shown=false;
    if(window.panel.isVisible)[window.panel orderOut:nil];
}
}

void open() {
    if(NSThread.isMainThread){open_now();return;}
    dispatch_async(dispatch_get_main_queue(),^{open_now();});
}

void close() {
    shown=false;
    if(NSThread.isMainThread){close_now();return;}
    dispatch_async(dispatch_get_main_queue(),^{close_now();});
}

// The panel's own state, so an order-out by AppKit cannot leave a stale flag.
bool visible() {
    if(!NSThread.isMainThread)return shown.load();
    const bool open=window.panel.isVisible;
    shown=open;
    return open;
}
bool owns_input(){return shown.load() || release_gate.pending();}
uint16_t filter_input(uint16_t buttons,bool physical_keys_held) {
    return uint16_t(release_gate.filter(buttons,shown.load(),physical_keys_held));
}

// Window thread, every frame.
void update() {
    if(!window)return;
    const std::string locale=localization::catalog().locale;
    void (^sync)(void)=^{
        if(!window)return;
        if(built_locale!=locale){built_locale=locale;[window retitle];}
        shown=window.panel.isVisible;
        if(shown)[window refresh];
    };
    if(NSThread.isMainThread)sync();
    else dispatch_async(dispatch_get_main_queue(),sync);
}

// Window thread while the host tears the window down: drop the panel before SDL
// quits its own, so AppKit cannot call back into an object that is going away.
void shutdown() {
    shown=false;
    void (^release)(void)=^{
        window.panel.delegate=nil;
        [window.panel orderOut:nil];
        window.panel.contentView=nil;
        window=nil;
    };
    if(NSThread.isMainThread)release();
    else dispatch_sync(dispatch_get_main_queue(),release);
}

// QA only. settings-control.json: {"schema":"srw64.settings-control.v1",
// "sequence":N,"action":"open"|"close"|"press","id":"<control id>"}.
void control(const std::filesystem::path& directory) {
    if(!std::getenv("SRW64_WINDOW_CONTROL"))return;
    static uint64_t sequence{};
    std::ifstream file(directory/"settings-control.json");
    const auto command=nlohmann::json::parse(file,nullptr,false);
    if(!command.is_object() || command.value("schema","")!="srw64.settings-control.v1" ||
       command.value("sequence",uint64_t{})<=sequence)return;
    sequence=command.at("sequence");
    const auto action=command.value("action",std::string{}), wanted=command.value("id",std::string{});
    const auto path=directory;
    void (^apply)(void)=^{
        if(action=="open")open_now();
        else if(action=="close")close_now();
        nlohmann::json row={{"schema","srw64.settings-window-event.v1"},{"sequence",sequence},
                            {"action",action},{"id",wanted},{"vi",srw64_current_vi()},{"pressed",false}};
        if(action=="press")for(SRW64Setting* setting in window.settings) {
            if(std::string(setting.identifierText.UTF8String)!=wanted)continue;
            // A click flips a checkbox or lights a radio before the action runs;
            // the preset push buttons carry no state.
            if(std::string(setting.identifierText.UTF8String).rfind("preset:",0)!=0)
                setting.button.state=setting.button.state==NSControlStateValueOn?NSControlStateValueOff:NSControlStateValueOn;
            [window activate:setting.button];
            row["pressed"]=true;
            break;
        }
        nlohmann::json controls=nlohmann::json::array();
        for(SRW64Setting* setting in window.settings)
            controls.push_back({{"id",setting.identifierText.UTF8String},{"title",setting.button.title.UTF8String},
                                {"on",setting.button.state==NSControlStateValueOn},{"enabled",setting.button.isEnabled?true:false}});
        row["controls"]=controls;
        row["visible"]=visible();
        row["enabled_rules"]=rules::names(rules::active_fixes());
        row["locale"]=localization::catalog().locale;
        std::ofstream(path/"settings-window-events.jsonl",std::ios::app)<<row.dump()<<'\n';
    };
    if(NSThread.isMainThread)apply();
    else dispatch_sync(dispatch_get_main_queue(),apply);
}
}
