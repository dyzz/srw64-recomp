#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include "debug_ui.hpp"
#include "debug_protocol.hpp"
#include <cmath>
#include <map>
#include <string>

// AppKit side of the debug interface. See debug_ui.hpp for the coordinate and
// window conventions. Clicks are real NSEvents when the app is active; when it
// is not (Claude drives the game while another app is in front), AppKit would
// spend the first click on activating the window, so a control is pressed with
// performClick: and a text field is focused directly instead.

namespace srw64::debug_ui {
namespace {
NSWindow* game_window;
using json=nlohmann::json;

std::string utf8(NSString* value){return value?std::string(value.UTF8String):std::string();}
NSString* ns(const std::string& value){return [NSString stringWithUTF8String:value.c_str()];}
[[noreturn]] void fail(int code,const std::string& message){throw debug::RpcError(code,message);}

bool metal_view(NSView* view) {
    return [view.layer isKindOfClass:[CAMetalLayer class]] || [NSStringFromClass([view class]) containsString:@"Metal"];
}
NSWindow* find_window(const json& selector) {
    if(selector.is_null() || selector=="game") {
        if(!game_window)fail(debug::ServerError,"the game window is not open yet");
        return game_window;
    }
    if(selector=="key") {
        if(!NSApp.keyWindow)fail(debug::InvalidParams,"no window of the game is key (another app is in front)");
        return NSApp.keyWindow;
    }
    if(selector.is_number_integer()) {
        if(NSWindow* window=[NSApp windowWithWindowNumber:selector.get<NSInteger>()])return window;
        fail(debug::InvalidParams,"no window with that number");
    }
    if(selector.is_string()) {
        NSString* wanted=ns(selector.get<std::string>());
        for(NSWindow* window in NSApp.windows)
            if(window.visible && [window.title rangeOfString:wanted options:NSCaseInsensitiveSearch].location!=NSNotFound)return window;
        fail(debug::InvalidParams,"no visible window titled like '"+selector.get<std::string>()+"'");
    }
    fail(debug::InvalidParams,"window must be \"game\", \"key\", a window number or a title");
}
// Top-left content coordinates of a view's frame.
NSRect content_rect(NSView* view,NSView* content) {
    NSRect r=[view convertRect:view.bounds toView:content];
    if(!content.isFlipped)r.origin.y=content.bounds.size.height-r.origin.y-r.size.height;
    return r;
}
std::string text_of(NSView* view) {
    if([view isKindOfClass:[NSButton class]])return utf8(((NSButton*)view).title);
    if([view isKindOfClass:[NSTextField class]])return utf8(((NSTextField*)view).stringValue);
    return {};
}
bool focused(NSView* view) {
    id responder=view.window.firstResponder;
    if(responder==view)return true;
    // An edited text field hands focus to the window's field editor.
    return [responder isKindOfClass:[NSTextView class]] && ((NSTextView*)responder).delegate==(id)view;
}
json describe(NSView* view,NSView* content,int depth) {
    const NSRect r=content_rect(view,content);
    json node={{"class",utf8(NSStringFromClass([view class]))},
               {"frame",{std::lround(r.origin.x),std::lround(r.origin.y),std::lround(r.size.width),std::lround(r.size.height)}}};
    if(metal_view(view)){node["role"]="game";return node;}
    if(auto text=text_of(view);!text.empty())node["text"]=text;
    if([view isKindOfClass:[NSTextField class]]) {
        auto* field=(NSTextField*)view;
        node["editable"]=bool(field.editable);
        if(field.placeholderString.length)node["placeholder"]=utf8(field.placeholderString);
    }
    if([view isKindOfClass:[NSButton class]])node["state"]=((NSButton*)view).state==NSControlStateValueOn;
    if([view isKindOfClass:[NSControl class]])node["enabled"]=bool(((NSControl*)view).enabled);
    if(view.accessibilityLabel.length)node["label"]=utf8(view.accessibilityLabel);
    if(focused(view))node["focused"]=true;
    if(depth<12) {
        json children=json::array();
        for(NSView* child in view.subviews)if(!child.hidden)children.push_back(describe(child,content,depth+1));
        if(!children.empty())node["children"]=children;
    }
    return node;
}
json window_json(NSWindow* window,bool views) {
    NSView* content=window.contentView;
    json value={{"number",window.windowNumber},{"title",utf8(window.title)},{"key",bool(window.keyWindow)},
                {"game",window==game_window},{"panel",bool([window isKindOfClass:[NSPanel class]])},
                {"size",{std::lround(content.bounds.size.width),std::lround(content.bounds.size.height)}},
                {"scale",window.backingScaleFactor}};
    if(views)value["views"]=describe(content,content,0);
    return value;
}
// Depth-first search for the first visible view whose text, placeholder or
// accessibility label is the wanted text; exact matches win over substrings.
NSView* find_view(NSView* view,NSString* wanted,bool exact) {
    if(view.hidden)return nil;
    NSArray<NSString*>* labels=@[ns(text_of(view)),view.accessibilityLabel?:@"",
        [view isKindOfClass:[NSTextField class]]?(((NSTextField*)view).placeholderString?:@""):@""];
    for(NSString* label in labels) {
        if(!label.length)continue;
        if(exact?[label isEqualToString:wanted]:[label rangeOfString:wanted].location!=NSNotFound)return view;
    }
    for(NSView* child in view.subviews)if(NSView* found=find_view(child,wanted,exact))return found;
    return nil;
}
NSEvent* mouse(NSEventType type,NSWindow* window,NSPoint point,NSInteger clicks) {
    return [NSEvent mouseEventWithType:type location:point modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime
                          windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:clicks pressure:1];
}
const std::map<std::string,std::pair<unsigned short,NSString*>>& keys() {
    static const std::map<std::string,std::pair<unsigned short,NSString*>> table=[] {
        std::map<std::string,std::pair<unsigned short,NSString*>> t{
            {"return",{36,@"\r"}},{"enter",{36,@"\r"}},{"tab",{48,@"\t"}},{"space",{49,@" "}},
            {"escape",{53,@"\x1b"}},{"esc",{53,@"\x1b"}},{"delete",{51,@"\x7f"}},{"backspace",{51,@"\x7f"}},
            {"forward_delete",{117,[NSString stringWithFormat:@"%C",(unichar)NSDeleteFunctionKey]}},
            {"left",{123,[NSString stringWithFormat:@"%C",(unichar)NSLeftArrowFunctionKey]}},
            {"right",{124,[NSString stringWithFormat:@"%C",(unichar)NSRightArrowFunctionKey]}},
            {"down",{125,[NSString stringWithFormat:@"%C",(unichar)NSDownArrowFunctionKey]}},
            {"up",{126,[NSString stringWithFormat:@"%C",(unichar)NSUpArrowFunctionKey]}}};
        const unsigned short letters[]={0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
        for(int i=0;i<26;++i)t[std::string(1,char('a'+i))]={letters[i],[NSString stringWithFormat:@"%c",'a'+i]};
        const unsigned short digits[]={29,18,19,20,21,23,22,26,28,25};
        for(int i=0;i<10;++i)t[std::string(1,char('0'+i))]={digits[i],[NSString stringWithFormat:@"%c",'0'+i]};
        const unsigned short function[]={122,120,99,118,96,97,98,100,101,109,103,111};
        for(int i=0;i<12;++i)t["f"+std::to_string(i+1)]={function[i],[NSString stringWithFormat:@"%C",(unichar)(NSF1FunctionKey+i)]};
        return t;
    }();
    return table;
}
}

void window_init(void* cocoa_window){game_window=(__bridge NSWindow*)cocoa_window;}

void close_game_window() {
    // The close button's path through Cocoa and the SDL delegate, not SDL_QUIT.
    if(!game_window)fail(debug::ServerError,"the game window is not open yet");
    [game_window performClose:nil];
}

json tree(const json& params) {
    json windows=json::array();
    if(params.contains("window"))windows.push_back(window_json(find_window(params["window"]),true));
    else for(NSWindow* window in NSApp.windows)if(window.visible)windows.push_back(window_json(window,true));
    return {{"windows",windows}};
}

json summary() {
    json windows=json::array();
    for(NSWindow* window in NSApp.windows)if(window.visible)windows.push_back(window_json(window,false));
    json focus=nullptr;
    if(NSWindow* key=NSApp.keyWindow) {
        id responder=key.firstResponder;
        if([responder isKindOfClass:[NSTextView class]] && [((NSTextView*)responder).delegate isKindOfClass:[NSView class]])
            responder=((NSTextView*)responder).delegate;
        if([responder isKindOfClass:[NSView class]])
            focus={{"window",key.windowNumber},{"class",utf8(NSStringFromClass([responder class]))},{"text",text_of(responder)}};
    }
    return {{"active",bool(NSApp.active)},{"windows",windows},{"focus",focus}};
}

json click(const json& params) {
    NSWindow* window=find_window(params.value("window",json(nullptr)));
    NSView* content=window.contentView;
    NSPoint point{};
    NSView* target=nil;
    if(params.contains("text")) {
        NSString* wanted=ns(params["text"].get<std::string>());
        target=find_view(content,wanted,true)?:find_view(content,wanted,false);
        if(!target)fail(debug::InvalidParams,"no visible control shows '"+params["text"].get<std::string>()+"'");
        const NSRect r=content_rect(target,content);
        point=NSMakePoint(NSMidX(r),NSMidY(r));
    } else {
        if(!params.contains("x") || !params.contains("y"))fail(debug::InvalidParams,"click needs x and y, or text");
        point=NSMakePoint(params["x"].get<double>(),params["y"].get<double>());
    }
    const NSPoint local=NSMakePoint(point.x,content.isFlipped?point.y:content.bounds.size.height-point.y);
    const NSPoint in_window=[content convertPoint:local toView:nil];
    NSView* hit=[content hitTest:[content.superview convertPoint:in_window fromView:nil]]?:content;
    const bool right=params.value("button","left")=="right";
    const int clicks=std::max(1,params.value("count",1));
    std::string delivery="events";
    if(!NSApp.active || !window.keyWindow) {
        // Background: skip AppKit's activate-on-first-click and act on the control.
        NSView* control=hit;
        while(control && ![control isKindOfClass:[NSControl class]])control=control.superview;
        if([control isKindOfClass:[NSTextField class]] && ((NSTextField*)control).editable) {
            [window makeFirstResponder:control];delivery="focus";
        } else if([control isKindOfClass:[NSButton class]] && !right) {
            for(int i=0;i<clicks;++i)[(NSButton*)control performClick:nil];
            delivery="perform_click";
        } else delivery.clear();
    }
    if(delivery=="events" || delivery.empty()) {
        delivery="events";
        const NSEventType down=right?NSEventTypeRightMouseDown:NSEventTypeLeftMouseDown;
        const NSEventType up=right?NSEventTypeRightMouseUp:NSEventTypeLeftMouseUp;
        for(int i=1;i<=clicks;++i) {
            // A control's tracking loop waits for the mouse-up: queue it first.
            [NSApp postEvent:mouse(up,window,in_window,i) atStart:NO];
            [NSApp sendEvent:mouse(down,window,in_window,i)];
            if(NSEvent* pending=[NSApp nextEventMatchingMask:NSEventMaskFromType(up) untilDate:NSDate.distantPast
                                                      inMode:NSDefaultRunLoopMode dequeue:YES])
                [NSApp sendEvent:pending];
        }
    }
    const NSRect r=content_rect(hit,content);
    return {{"window",window.windowNumber},{"point",{point.x,point.y}},{"delivery",delivery},
            {"hit",{{"class",utf8(NSStringFromClass([hit class]))},{"text",text_of(hit)},
                    {"frame",{std::lround(r.origin.x),std::lround(r.origin.y),std::lround(r.size.width),std::lround(r.size.height)}}}}};
}

json key(const json& params) {
    NSWindow* window=find_window(params.value("window",json(nullptr)));
    unsigned short code=0;NSString* characters=@"";
    if(params.contains("key")) {
        auto name=params["key"].get<std::string>();
        for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
        const auto found=keys().find(name);
        if(found==keys().end())fail(debug::InvalidParams,"unknown key '"+name+"'");
        code=found->second.first;characters=found->second.second;
    } else if(params.contains("key_code")) {
        code=params["key_code"].get<unsigned short>();characters=ns(params.value("characters",""));
    } else fail(debug::InvalidParams,"key needs key, or key_code and characters");
    NSEventModifierFlags flags=0;
    for(const auto& m:params.value("modifiers",json::array())) {
        const auto name=m.get<std::string>();
        if(name=="cmd" || name=="command")flags|=NSEventModifierFlagCommand;
        else if(name=="shift")flags|=NSEventModifierFlagShift;
        else if(name=="option" || name=="alt")flags|=NSEventModifierFlagOption;
        else if(name=="control" || name=="ctrl")flags|=NSEventModifierFlagControl;
        else fail(debug::InvalidParams,"unknown modifier '"+name+"'");
    }
    NSString* shown=(flags&NSEventModifierFlagShift)?characters.uppercaseString:characters;
    for(auto type:{NSEventTypeKeyDown,NSEventTypeKeyUp}) {
        NSEvent* event=[NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:flags
            timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.windowNumber context:nil
            characters:shown charactersIgnoringModifiers:characters isARepeat:NO keyCode:code];
        [NSApp sendEvent:event];
    }
    return {{"window",window.windowNumber},{"key_code",code},{"focus",summary()["focus"]}};
}

json type(const json& params) {
    NSWindow* window=find_window(params.value("window",json(nullptr)));
    const bool unmark=params.value("unmark",false);
    if(!unmark && (!params.contains("text") || !params["text"].is_string()))fail(debug::InvalidParams,"type needs text");
    id responder=window.firstResponder;
    if([responder isKindOfClass:[NSTextField class]]) {
        [window makeFirstResponder:responder];responder=((NSTextField*)responder).currentEditor;
    }
    if(![responder isKindOfClass:[NSTextView class]])fail(debug::InvalidParams,"no text field has focus in that window; click one first");
    auto* editor=(NSTextView*)responder;
    // "marked" leaves the text as an input-method composition, as an IME does
    // before the candidate is chosen; "unmark" commits the composition.
    if(unmark)[editor unmarkText];
    else if(params.value("marked",false)) {
        NSString* text=ns(params["text"].get<std::string>());
        [editor setMarkedText:text selectedRange:NSMakeRange(text.length,0) replacementRange:NSMakeRange(NSNotFound,0)];
    } else [editor insertText:ns(params["text"].get<std::string>()) replacementRange:NSMakeRange(NSNotFound,0)];
    id owner=((NSTextView*)responder).delegate;
    return {{"window",window.windowNumber},{"composing",bool(editor.hasMarkedText)},
            {"text",[owner isKindOfClass:[NSView class]]?text_of(owner):utf8(editor.string)}};
}

json menu(const json& params) {
    NSMenu* current=NSApp.mainMenu;
    if(!current)fail(debug::ServerError,"the menu bar is not ready");
    const auto path=params.value("path",json::array());
    auto list=[](NSMenu* menu) {
        json items=json::array();
        for(NSMenuItem* item in menu.itemArray) {
            if(item.separatorItem)continue;
            items.push_back({{"title",utf8(item.title)},{"enabled",bool(item.enabled)},
                             {"checked",item.state==NSControlStateValueOn},{"submenu",item.hasSubmenu}});
        }
        return items;
    };
    for(size_t i=0;i<path.size();++i) {
        NSString* wanted=ns(path[i].get<std::string>());
        NSInteger index=-1;
        for(NSInteger j=0;j<current.numberOfItems;++j)if([[current itemAtIndex:j].title isEqualToString:wanted]){index=j;break;}
        if(index<0)for(NSInteger j=0;j<current.numberOfItems;++j)
            if([[current itemAtIndex:j].title rangeOfString:wanted].location!=NSNotFound){index=j;break;}
        if(index<0)fail(debug::InvalidParams,"no menu item '"+path[i].get<std::string>()+"'");
        NSMenuItem* item=[current itemAtIndex:index];
        if(item.hasSubmenu){current=item.submenu;continue;}
        if(i+1!=path.size())fail(debug::InvalidParams,"'"+utf8(item.title)+"' has no submenu");
        if(!item.enabled)fail(debug::InvalidParams,"'"+utf8(item.title)+"' is disabled");
        [current performActionForItemAtIndex:index];
        return {{"pressed",utf8(item.title)},{"items",list(current)}};
    }
    return {{"items",list(current)}};
}

json compose(const std::filesystem::path& png) {
    if(!game_window)return {{"overlays",json::array()}};
    NSView* content=game_window.contentView;
    NSMutableArray<NSView*>* overlays=[NSMutableArray array];
    // SDL's own views (the Metal view, its text-input responder) are not overlays.
    for(NSView* view in content.subviews)
        if(!view.hidden && view.alphaValue>0 && !metal_view(view) && ![NSStringFromClass([view class]) hasPrefix:@"SDL"])
            [overlays addObject:view];
    json names=json::array();
    for(NSView* view in overlays)names.push_back(utf8(NSStringFromClass([view class])));
    if(!overlays.count)return {{"overlays",names}};
    NSBitmapImageRep* base=[NSBitmapImageRep imageRepWithContentsOfFile:ns(png.string())];
    if(!base)fail(debug::ServerError,"cannot read "+png.string());
    const NSInteger width=base.pixelsWide,height=base.pixelsHigh;
    NSBitmapImageRep* canvas=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:width pixelsHigh:height
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext=[NSGraphicsContext graphicsContextWithBitmapImageRep:canvas];
    [base drawInRect:NSMakeRect(0,0,width,height)];
    const double sx=width/content.bounds.size.width,sy=height/content.bounds.size.height;
    for(NSView* view in overlays) {
        NSBitmapImageRep* rep=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
        NSRect r=[view convertRect:view.bounds toView:content];
        if(content.isFlipped)r.origin.y=content.bounds.size.height-r.origin.y-r.size.height;
        [rep drawInRect:NSMakeRect(r.origin.x*sx,r.origin.y*sy,r.size.width*sx,r.size.height*sy) fromRect:NSZeroRect
              operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    }
    [NSGraphicsContext restoreGraphicsState];
    if(![[canvas representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:ns(png.string()) atomically:YES])
        fail(debug::ServerError,"cannot write "+png.string());
    return {{"overlays",names}};
}

json capture(const json& params,const std::filesystem::path& png) {
    NSWindow* window=find_window(params.value("window",json(nullptr)));
    NSView* content=window.contentView;
    NSBitmapImageRep* rep=[content bitmapImageRepForCachingDisplayInRect:content.bounds];
    [content cacheDisplayInRect:content.bounds toBitmapImageRep:rep];
    if(![[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:ns(png.string()) atomically:YES])
        fail(debug::ServerError,"cannot write "+png.string());
    return {{"path",png.string()},{"window",window.windowNumber},{"width",rep.pixelsWide},{"height",rep.pixelsHigh},
            {"note",window==game_window?"AppKit views only: the game picture comes from screenshot":""}};
}
}
