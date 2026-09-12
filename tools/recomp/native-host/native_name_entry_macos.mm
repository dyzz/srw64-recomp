#import <Cocoa/Cocoa.h>
#include "native_name_entry.hpp"
#include "native_dialogue.hpp"
#include "localization/catalog.hpp"
#include "presentation/image_mode.hpp"
#include "json/json.hpp"
#include <fstream>

namespace {
NSString* string(const std::string& text) {return [NSString stringWithUTF8String:text.c_str()];}
NSString* label(const char* key) {return string(srw64::localization::catalog().ui(key));}
std::u16string text(NSString* value) {return srw64::dialogue::utf16(value.UTF8String);}
NSString* string(const std::u16string& value) {return string(srw64::dialogue::utf8(value));}
NSWindow* game_window;
std::filesystem::path output;
NSColor* rgb(unsigned color,CGFloat alpha=1) {
    return [NSColor colorWithSRGBRed:((color>>16)&255)/255. green:((color>>8)&255)/255. blue:(color&255)/255. alpha:alpha];
}
void box(NSRect rect,unsigned color,CGFloat radius=0,CGFloat alpha=1) {
    [rgb(color,alpha) setFill];[[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] fill];
}
void stroke_box(NSRect rect,unsigned color,CGFloat radius=0,CGFloat width=1) {
    [rgb(color) setStroke];auto* path=[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius];
    path.lineWidth=width;[path stroke];
}
void draw(NSString* value,NSRect rect,CGFloat size,unsigned color,BOOL bold=NO) {
    NSMutableParagraphStyle* style=[[NSMutableParagraphStyle alloc] init];style.lineBreakMode=NSLineBreakByTruncatingTail;
    [value drawInRect:rect withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size weight:bold?NSFontWeightSemibold:NSFontWeightRegular],
        NSForegroundColorAttributeName:rgb(color),NSParagraphStyleAttributeName:style}];
}
const char* field_keys[]={"name_given","name_family","name_nickname"};
}

@interface SRW64NameField : NSTextField
@end
@implementation SRW64NameField
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    // SDL has no Edit menu. Route standard shortcuts to the native field editor.
    if((event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask)==NSEventModifierFlagCommand) {
        NSDictionary* actions=@{@"a":@"selectAll:",@"c":@"copy:",@"v":@"paste:",@"x":@"cut:",@"z":@"undo:"};
        NSString* action=actions[event.charactersIgnoringModifiers.lowercaseString];NSText* editor=self.currentEditor;
        if(action && editor && [editor respondsToSelector:NSSelectorFromString(action)]) {
            [NSApp sendAction:NSSelectorFromString(action) to:editor from:self];return YES;
        }
    }
    return [super performKeyEquivalent:event];
}
@end

@interface SRW64NameButton : NSButton
@property BOOL primary;
@end
@implementation SRW64NameButton
- (void)drawRect:(NSRect)dirty {
    CGFloat scale=self.bounds.size.height/48;
    if(self.primary)box(self.bounds,self.cell.highlighted?0x65C6E5:0x9BE4F7,9*scale,self.enabled?1:.4);
    else {box(self.bounds,0x122131,9*scale);stroke_box(NSInsetRect(self.bounds,.5,.5),0x2E4356,9*scale);}
    auto font=[NSFont systemFontOfSize:15*scale weight:NSFontWeightSemibold];
    auto attrs=@{NSFontAttributeName:font,NSForegroundColorAttributeName:rgb(self.primary?0x0A2736:(self.enabled?0xC5D4E2:0x566879))};
    auto size=[self.title sizeWithAttributes:attrs];
    [self.title drawAtPoint:NSMakePoint((self.bounds.size.width-size.width)/2,(self.bounds.size.height-size.height)/2) withAttributes:attrs];
}
@end

@class SRW64NameController;
@interface SRW64NamePage : NSView
@property(weak) SRW64NameController* owner;
- (NSRect)place:(NSRect)rect;
- (CGFloat)scale;
@end
@interface SRW64NameController : NSObject <NSTextFieldDelegate> {
@public
    srw64::names::Request request;
    std::array<std::u16string,3> values;
    unsigned field;
    SRW64NamePage* page;
    NSArray<SRW64NameField*>* inputs;
    SRW64NameButton *next,*reset,*cancel;
    NSArray<NSImage*>* portraits;
    NSString* error;
    bool waiting;
    int image_mode;
    NSResponder* previousResponder;
}
- (void)show:(const srw64::names::Request&)value;
- (void)refresh;
- (void)arrange;
- (void)focus:(unsigned)index;
- (void)advance:(id)sender;
- (void)commit:(id)sender;
- (void)goBack:(id)sender;
- (void)useDefault:(id)sender;
- (void)cancel:(id)sender;
- (void)snapshot:(NSString*)suffix;
@end

@implementation SRW64NamePage
- (BOOL)isFlipped {return YES;}
- (BOOL)isOpaque {return YES;}
- (BOOL)acceptsFirstResponder {return YES;}
- (void)keyDown:(NSEvent*)event {
    if(self.owner->request.person==srw64::names::Review) {
        if(event.keyCode==36 || event.keyCode==76){[self.owner commit:nil];return;}
        if(event.keyCode==53){[self.owner cancel:nil];return;}
    }
    [super keyDown:event];
}
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if(self.owner->request.person==srw64::names::Review && !self.hidden) {
        if([event.charactersIgnoringModifiers isEqualToString:@"\r"]){[self.owner commit:nil];return YES;}
        if(event.keyCode==53){[self.owner cancel:nil];return YES;}
    }
    return [super performKeyEquivalent:event];
}
- (CGFloat)scale {return MIN(self.bounds.size.width/1080.,self.bounds.size.height/720.);}
- (NSRect)place:(NSRect)rect {
    CGFloat s=self.scale;return NSMakeRect((self.bounds.size.width-1080*s)/2+rect.origin.x*s,
        (self.bounds.size.height-720*s)/2+rect.origin.y*s,rect.size.width*s,rect.size.height*s);
}
- (void)setFrameSize:(NSSize)size {[super setFrameSize:size];[self.owner arrange];self.needsDisplay=YES;}
- (void)drawRect:(NSRect)dirty {
    srw64::localization::Scope language(srw64::localization::snapshot());
    box(self.bounds,0x070C13);
    auto* c=self.owner;if(!c)return;
    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform* transform=[NSAffineTransform transform];auto origin=[self place:NSMakeRect(0,0,0,0)].origin;
    [transform translateXBy:origin.x yBy:origin.y];[transform scaleBy:self.scale];[transform concat];
    box(NSMakeRect(0,0,420,720),0x0B1723);
    box(NSMakeRect(0,0,6,720),0x89DBF1);
    draw(@"SRW 64",NSMakeRect(54,36,180,24),18,0xD9F3FA,YES);
    draw(label("name_title"),NSMakeRect(202,39,220,22),13,0x718DA4);
    const char* steps[]={"name_step_player","name_step_partner","name_step_review"};
    for(unsigned i=0;i<3;++i) {
        CGFloat x=548+i*169;BOOL active=c->request.person==i,done=c->request.person>i;
        box(NSMakeRect(x,34,28,28),active?0x9BE4F7:0x182B3B,14);
        draw(done?@"✓":[NSString stringWithFormat:@"%u",i+1],NSMakeRect(x+9,39,18,20),13,active?0x0A2736:0x8DA8BD,YES);
        draw(label(steps[i]),NSMakeRect(x+38,39,125,22),13,active?0xE7F3FC:0x748DA1,active);
    }
    box(NSMakeRect(54,88,972,1),0x203345);
    const bool review=c->request.person==srw64::names::Review;
    draw(label(review?"name_review":c->request.person?"name_partner":"name_player"),NSMakeRect(54,115,972,47),32,0xEDF5FC,YES);
    draw(label(review?"name_review_hint":"name_page_hint"),NSMakeRect(54,169,970,30),14,0x8DA6BA);
    auto portrait=[&](unsigned person,NSRect rect) {
        box(rect,0x183043,12);
        if(person<c->portraits.count) {
            [NSGraphicsContext saveGraphicsState];
            [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:12 yRadius:12] addClip];
            NSGraphicsContext.currentContext.imageInterpolation=c->image_mode==1?NSImageInterpolationHigh:NSImageInterpolationNone;
            [c->portraits[person] drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
            [NSGraphicsContext restoreGraphicsState];
        }
    };
    if(review) {
        for(unsigned p=0;p<2;++p) {
            CGFloat x=54+p*504;box(NSMakeRect(x,226,468,340),0x101E2D,14);stroke_box(NSMakeRect(x,226,468,340),0x294254,14);
            portrait(p,NSMakeRect(x+24,250,144,144));
            draw(label(steps[p]),NSMakeRect(x+194,255,244,25),13,0x8CD6EF,YES);
            draw(string(c->request.names[p][0]),NSMakeRect(x+194,291,244,40),28,0xF0F6FC,YES);
            draw(string(c->request.names[p][1]),NSMakeRect(x+194,337,244,34),22,0xADC4D6);
            box(NSMakeRect(x+24,420,420,1),0x284052);
            draw(label("name_nickname"),NSMakeRect(x+24,445,420,23),13,0x809DB4);
            draw(string(c->request.names[p][2]),NSMakeRect(x+24,480,420,42),27,0xF2B678,YES);
        }
    } else {
        portrait(c->request.person,NSMakeRect(82,224,280,280));
        draw(label("name_preview"),NSMakeRect(54,526,326,22),12,0x6C8BA2);
        draw([NSString stringWithFormat:@"%@ · %@",string(c->values[0]),string(c->values[1])],NSMakeRect(54,556,328,35),22,0xEBF4FC,YES);
        draw([NSString stringWithFormat:@"%@  %@",label("name_nickname"),string(c->values[2])],NSMakeRect(54,599,328,27),16,0xEAB27A);
        for(unsigned f=0;f<3;++f) {
            CGFloat y=224+f*110;BOOL active=c->field==f;
            box(NSMakeRect(462,y,564,94),0x101E2D,10);
            stroke_box(NSMakeRect(462,y,564,94),active?(c->error.length?0xEDAC70:0x81D6EE):0x2A3E50,10,active?1.6:1);
            draw(label(field_keys[f]),NSMakeRect(482,y+11,400,23),13,active?0x94DCEF:0x839EB3,YES);
            draw([NSString stringWithFormat:@"%lu / %u",(unsigned long)c->inputs[f].stringValue.length,f==2?5:7],NSMakeRect(964,y+12,48,21),12,0x7D98AC);
        }
        draw(c->error?:@"",NSMakeRect(462,550,564,47),13,0xF1B276);
    }
    draw(label(review?"name_review_keyboard_hint":"name_keyboard_hint"),NSMakeRect(54,676,972,23),12,0x6E879C);
    [NSGraphicsContext restoreGraphicsState];
}
@end

@implementation SRW64NameController
- (instancetype)init {
    if(!(self=[super init]))return nil;
    page=[[SRW64NamePage alloc] initWithFrame:game_window.contentView.bounds];page.owner=self;
    page.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;page.wantsLayer=YES;
    page.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    NSMutableArray* fields=[NSMutableArray array];
    for(unsigned f=0;f<3;++f) {
        auto* input=[[SRW64NameField alloc] init];input.delegate=self;input.tag=f;
        input.bordered=NO;input.bezeled=NO;input.drawsBackground=YES;input.backgroundColor=rgb(0x101E2D);input.textColor=rgb(0xF0F5FA);
        input.focusRingType=NSFocusRingTypeNone;input.usesSingleLineMode=YES;input.cell.scrollable=YES;
        input.accessibilityLabel=label(field_keys[f]);[page addSubview:input];[fields addObject:input];
    }
    inputs=fields;
    for(unsigned f=0;f<3;++f)inputs[f].nextKeyView=inputs[(f+1)%3];
    auto button=[&](SEL action) {
        auto* value=[[SRW64NameButton alloc] init];value.target=self;value.action=action;value.bordered=NO;
        value.buttonType=NSButtonTypeMomentaryPushIn;[page addSubview:value];return value;
    };
    cancel=button(@selector(cancel:));reset=button(@selector(useDefault:));next=button(@selector(commit:));next.primary=YES;
    return self;
}
- (void)arrange {
    CGFloat s=page.scale;
    for(unsigned f=0;f<3;++f) {
        inputs[f].frame=[page place:NSMakeRect(478,264+110*f,528,39)];
        inputs[f].font=[NSFont systemFontOfSize:26*s weight:NSFontWeightMedium];
    }
    bool review=request.person==srw64::names::Review;
    cancel.frame=[page place:NSMakeRect(review?54:462,612,184,48)];
    reset.frame=[page place:NSMakeRect(660,612,146,48)];
    next.frame=[page place:NSMakeRect(822,612,204,48)];
}
- (void)show:(const srw64::names::Request&)value {
    request=value;values=value.values;field=0;waiting=!value.active;error=@"";
    image_mode=-99;
    if(!page.superview || page.hidden)previousResponder=game_window.firstResponder;
    if(!page.superview)[game_window.contentView addSubview:page positioned:NSWindowAbove relativeTo:nil];
    page.frame=game_window.contentView.bounds;page.hidden=NO;
    for(unsigned f=0;f<3;++f)inputs[f].stringValue=string(values[f]);
    [self refresh];
}
- (void)refresh {
    const bool review=request.person==srw64::names::Review;
    if(image_mode!=srw64::presentation::image_mode.current()) {
        image_mode=srw64::presentation::image_mode.current();NSMutableArray* images=[NSMutableArray array];
        for(unsigned p=0;p<2;++p) {
            auto* image=[[NSImage alloc] initWithContentsOfFile:string(request.portraits[p][image_mode==1])];
            [images addObject:image?:[[NSImage alloc] initWithSize:NSMakeSize(96,96)]];
        }
        portraits=images;
    }
    for(SRW64NameField* input in inputs){input.hidden=review;input.enabled=!waiting;}
    next.enabled=reset.enabled=cancel.enabled=!waiting;
    next.title=label(review?"name_start":request.person?"name_to_review":"name_to_partner");
    cancel.title=label(review?"name_edit":"name_cancel");reset.title=label("name_default");reset.hidden=review;
    next.keyEquivalent=review?@"\r":@"";cancel.keyEquivalent=review?@"\e":@"";
    [self arrange];page.needsDisplay=YES;
    if(!waiting && !review && !inputs[field].currentEditor)[self focus:field];
    // NSButton focus depends on the user's Full Keyboard Access setting.
    // Give the review page its own responder for reliable Return / Escape.
    if(review)[game_window makeFirstResponder:page];
}
- (void)focus:(unsigned)index {
    if(waiting || request.person==srw64::names::Review || index>2)return;
    field=index;[game_window makeFirstResponder:inputs[field]];[inputs[field] selectText:nil];page.needsDisplay=YES;
}
- (BOOL)composing {
    for(SRW64NameField* input in inputs)if([(NSTextView*)input.currentEditor hasMarkedText])return YES;
    return NO;
}
- (void)controlTextDidBeginEditing:(NSNotification*)note {field=(unsigned)[note.object tag];page.needsDisplay=YES;}
- (void)controlTextDidChange:(NSNotification*)note {
    auto* input=(NSTextField*)note.object;field=(unsigned)input.tag;values[field]=text(input.stringValue);error=@"";page.needsDisplay=YES;
}
- (void)advance:(id)sender {
    if(waiting || [self composing])return;
    if(request.person==srw64::names::Review){[self commit:nil];return;}
    auto problem=srw64::names::validate(text([inputs[field].stringValue precomposedStringWithCanonicalMapping]),field);
    if(!problem.empty()){error=label(problem.c_str());page.needsDisplay=YES;return;}
    if(field<2)[self focus:field+1];else [self commit:nil];
}
- (void)commit:(id)sender {
    if(waiting || [self composing])return;
    if(request.person==srw64::names::Review)srw64::names::review(request.serial,true);
    else {
        for(unsigned f=0;f<3;++f) {
            values[f]=text([inputs[f].stringValue precomposedStringWithCanonicalMapping]);
            auto problem=srw64::names::validate(values[f],f);
            if(!problem.empty()){error=label(problem.c_str());[self focus:f];return;}
        }
        srw64::names::submit(request.serial,values);
    }
    waiting=true;[game_window makeFirstResponder:nil];[self refresh];
}
- (void)goBack:(id)sender {if(field)[self focus:field-1];}
- (void)useDefault:(id)sender {
    if(waiting || [self composing])return;
    values=request.values;for(unsigned f=0;f<3;++f)inputs[f].stringValue=string(values[f]);error=@"";[self focus:field];
}
- (void)cancel:(id)sender {
    if(waiting || [self composing])return;
    if(request.person==srw64::names::Review)srw64::names::review(request.serial,false);
    else srw64::names::submit(request.serial,values,true);
    waiting=true;[game_window makeFirstResponder:nil];[self refresh];
}
- (BOOL)control:(NSControl*)control textView:(NSTextView*)view doCommandBySelector:(SEL)selector {
    if(selector==@selector(insertNewline:)){[self advance:nil];return YES;}
    if(selector==@selector(insertTab:)){[self focus:(field+1)%3];return YES;}
    if(selector==@selector(insertBacktab:)){[self focus:(field+2)%3];return YES;}
    if(selector==@selector(cancelOperation:)){[self cancel:nil];return YES;}
    return NO;
}
- (void)snapshot:(NSString*)suffix {
    [page displayIfNeeded];
    // View-only rendering is supplementary. QA also captures the real window
    // (including the Metal surface and native field editor) using window_id.
    NSBitmapImageRep* bitmap=[page bitmapImageRepForCachingDisplayInRect:page.bounds];
    [page cacheDisplayInRect:page.bounds toBitmapImageRep:bitmap];
    [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
        writeToFile:string((output/("name-entry-"+std::to_string(request.serial)+"-"+suffix.UTF8String+".png")).string()) atomically:YES];
}
@end

namespace srw64::names {
namespace {SRW64NameController* controller;}
void window_init(void* cocoa,const std::filesystem::path& directory) {game_window=(__bridge NSWindow*)cocoa;output=directory;}
void window_update() {
    @autoreleasepool {
        const auto language=localization::snapshot();localization::Scope scope(language);
        static localization::Snapshot previous_language;
        const auto value=request();
        if(value.visible && (!controller || controller->request.serial!=value.serial)) {
            window_claim_input(true);if(!controller)controller=[[SRW64NameController alloc] init];[controller show:value];
        }
        if(!controller)return;
        if(language!=previous_language) {
            previous_language=language;
            for(unsigned f=0;f<3;++f)controller->inputs[f].accessibilityLabel=label(field_keys[f]);
            controller->error=controller->request.error.empty()?@"":label(controller->request.error.c_str());
            [controller refresh];
        }
        if(!value.visible) {
            if(cover_in_flight())return;
            if(!controller->page.hidden) {
                controller->page.hidden=YES;[game_window makeFirstResponder:controller->previousResponder];
            }
            const unsigned game_keys[]={0,1,2,6,7,12,13,14,34,37,38,40,36,49,53,123,124,125,126};
            bool held=false;for(auto key:game_keys)held |= CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,key);
            window_claim_input(held);return;
        }
        if(value.revision!=controller->request.revision) {
            controller->request.revision=value.revision;controller->request.active=value.active;
            controller->request.error=value.error;controller->waiting=!value.active;
            controller->error=value.error.empty()?@"":label(value.error.c_str());[controller refresh];
        }
        if(controller->image_mode!=srw64::presentation::image_mode.current())[controller refresh];
        // Opt-in QA exercises the embedded field editor and real button actions.
        if(std::getenv("SRW64_NAME_ENTRY_CONTROL")) {
            static uint64_t sequence{};std::ifstream file(output/"name-entry-control.json");auto command=nlohmann::json::parse(file,nullptr,false);
            if(command.is_discarded() || !command.is_object())return;
            if(command.value("schema","")!="srw64.name-entry-control.v1" || command.value("sequence",uint64_t{})<=sequence ||
                command.value("serial",uint64_t{})!=value.serial || command.value("field",99u)!=controller->field || !value.active)return;
            sequence=command.at("sequence").get<uint64_t>();const auto action=command.value("action","");
            if(action=="insert" && value.person!=Review) {
                [controller focus:controller->field];
                [(NSTextView*)controller->inputs[controller->field].currentEditor insertText:string(command.value("text","")) replacementRange:NSMakeRange(NSNotFound,0)];
            } else if(action=="marked" && value.person!=Review) {
                [controller focus:controller->field];
                [(NSTextView*)controller->inputs[controller->field].currentEditor setMarkedText:string(command.value("text",""))
                    selectedRange:NSMakeRange(0,0) replacementRange:NSMakeRange(NSNotFound,0)];
            } else if(action=="unmark" && value.person!=Review) {
                [(NSTextView*)controller->inputs[controller->field].currentEditor unmarkText];
            } else if(action=="next") [controller advance:nil];
            else if(action=="commit") [controller commit:nil];
            else if(action=="back") [controller goBack:nil];
            else if(action=="default") [controller useDefault:nil];
            else if(action=="cancel") [controller cancel:nil];
            else if(action=="focus") [controller focus:command.value("index",0u)];
            else if(action=="tab" || action=="shift-tab")
                [(NSTextView*)controller->inputs[controller->field].currentEditor doCommandBySelector:action=="tab"?@selector(insertTab:):@selector(insertBacktab:)];
            else if(action=="key") {
                [game_window makeKeyAndOrderFront:nil];
                auto* characters=string(command.value("text",""));
                for(auto type:{NSEventTypeKeyDown,NSEventTypeKeyUp}) {
                    auto* event=[NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:0 timestamp:0
                        windowNumber:game_window.windowNumber context:nil characters:characters charactersIgnoringModifiers:characters
                        isARepeat:NO keyCode:command.value("key_code",0u)];
                    [NSApp sendEvent:event];
                }
            }
            else if(action=="resize") [game_window setContentSize:NSMakeSize(command.value("width",1080),command.value("height",720))];
            [controller snapshot:string(std::to_string(sequence))];
            nlohmann::json texts=nlohmann::json::array();for(SRW64NameField* input in controller->inputs)texts.push_back(input.stringValue.UTF8String);
            std::ofstream(output/"name-entry-ui.json")<<nlohmann::json({{"sequence",sequence},{"serial",value.serial},{"person",value.person},
                {"field",controller->field},{"text",controller->inputs[controller->field].stringValue.UTF8String},{"values",texts},
                {"error",controller->error.UTF8String},{"pending",controller->waiting},{"window_id",game_window.windowNumber},
                {"locale",localization::catalog().locale},{"composing",bool([controller composing])},
                {"embedded",controller->page.window==game_window},{"child_windows",game_window.childWindows.count},
                {"width",controller->page.bounds.size.width},{"height",controller->page.bounds.size.height}}).dump(2)<<'\n';
        }
    }
}
void window_shutdown() {
    if(controller){[controller->page removeFromSuperview];controller=nil;}game_window=nil;window_claim_input(false);
}
}
