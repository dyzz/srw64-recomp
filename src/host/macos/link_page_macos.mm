#import <Cocoa/Cocoa.h>
#include "link_page.hpp"
#include "debug_protocol.hpp"
#include "localization/catalog.hpp"
#include "presentation/image_mode.hpp"

// The Link Battler page, drawn on the protagonist selection page's grid and colours
// (native_name_entry_macos.mm): 1080 x 720 points at scale 1, three series cards.
namespace {
NSString* string(const std::string& text) {return [NSString stringWithUTF8String:text.c_str()]?:@"";}
NSString* label(const char* key) {return string(srw64::localization::catalog().ui(key));}
NSString* label(const std::string& key) {return label(key.c_str());}
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
void draw(NSString* value,NSRect rect,CGFloat size,unsigned color,BOOL bold=NO,NSTextAlignment align=NSTextAlignmentLeft) {
    NSMutableParagraphStyle* style=[[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode=NSLineBreakByTruncatingTail;style.alignment=align;
    [value drawInRect:rect withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size weight:bold?NSFontWeightSemibold:NSFontWeightRegular],
        NSForegroundColorAttributeName:rgb(color),NSParagraphStyleAttributeName:style}];
}
void portrait_in(NSImage* image,NSRect rect,CGFloat radius,bool smooth) {
    box(rect,0x183043,radius);
    if(!image)return;
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] addClip];
    NSGraphicsContext.currentContext.imageInterpolation=smooth?NSImageInterpolationHigh:NSImageInterpolationNone;
    [image drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    [NSGraphicsContext restoreGraphicsState];
}
const char* series_keys[]={"f91","goshogun","zambot"};
std::string key(const char* field,unsigned series) {return std::string("link_")+field+"_"+series_keys[series];}
NSWindow* game_window;
}

@interface SRW64LinkButton : NSButton
@property BOOL primary;
@end
@implementation SRW64LinkButton
- (void)drawRect:(NSRect)dirty {
    const CGFloat scale=self.bounds.size.height/48;
    if(self.primary)box(self.bounds,self.cell.highlighted?0x65C6E5:0x9BE4F7,9*scale,self.enabled?1:.4);
    else {box(self.bounds,0x122131,9*scale);stroke_box(NSInsetRect(self.bounds,.5,.5),0x2E4356,9*scale);}
    auto attrs=@{NSFontAttributeName:[NSFont systemFontOfSize:15*scale weight:NSFontWeightSemibold],
                 NSForegroundColorAttributeName:rgb(self.primary?0x0A2736:(self.enabled?0xC5D4E2:0x566879))};
    const auto size=[self.title sizeWithAttributes:attrs];
    [self.title drawAtPoint:NSMakePoint((self.bounds.size.width-size.width)/2,(self.bounds.size.height-size.height)/2) withAttributes:attrs];
}
@end

@class SRW64LinkController;
// One series. A button, so it takes clicks, Full Keyboard Access and the debug
// interface's click-by-text (its title is the series name); the controller draws it.
@interface SRW64LinkCard : NSButton
@property(weak) SRW64LinkController* owner;
@property unsigned series;
@end
@interface SRW64LinkPage : NSView
@property(weak) SRW64LinkController* owner;
- (NSRect)place:(NSRect)rect;
- (CGFloat)scale;
@end
@interface SRW64LinkController : NSObject {
@public
    srw64::link_page::Request request;
    SRW64LinkPage* page;
    NSArray<SRW64LinkCard*>* cards;
    SRW64LinkButton *back,*next;
    std::array<NSArray<NSImage*>*,3> portraits;
    std::array<bool,3> ticked;
    unsigned focus;
    int image_mode;
    bool waiting;
    bool releasing;   // page just closed; waiting once for its keys to be released
    NSResponder* previousResponder;
}
- (void)show:(const srw64::link_page::Request&)value;
- (void)refresh;
- (void)arrange;
- (void)move:(int)delta;
- (void)toggle:(unsigned)series;
- (void)pick:(SRW64LinkCard*)sender;
- (void)commit:(id)sender;
- (void)goBack:(id)sender;
- (bool)locked:(unsigned)series;
- (void)drawCard:(unsigned)series bounds:(NSRect)bounds;
@end

@implementation SRW64LinkCard
- (BOOL)isFlipped {return YES;}
- (void)drawRect:(NSRect)dirty {[self.owner drawCard:self.series bounds:self.bounds];}
@end

@implementation SRW64LinkPage
- (BOOL)isFlipped {return YES;}
- (BOOL)isOpaque {return YES;}
- (BOOL)acceptsFirstResponder {return YES;}
- (void)keyDown:(NSEvent*)event {
    // Arrows as on the protagonist page; Space or the game's Z ticks the focused
    // card, Return or Enter continues, Esc or the game's X goes back.
    switch(event.keyCode) {
        case 123:case 126:[self.owner move:-1];return;
        case 124:case 125:[self.owner move:1];return;
        case 49:case 6:[self.owner toggle:self.owner->focus];return;
        case 36:case 76:[self.owner commit:nil];return;
        case 53:case 7:[self.owner goBack:nil];return;
    }
    [super keyDown:event];
}
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if(self.hidden)return [super performKeyEquivalent:event];
    if([event.charactersIgnoringModifiers isEqualToString:@"\r"]){[self.owner commit:nil];return YES;}
    if(event.keyCode==53){[self.owner goBack:nil];return YES;}
    return [super performKeyEquivalent:event];
}
- (CGFloat)scale {return MIN(self.bounds.size.width/1080.,self.bounds.size.height/720.);}
- (NSRect)place:(NSRect)rect {
    const CGFloat s=self.scale;return NSMakeRect((self.bounds.size.width-1080*s)/2+rect.origin.x*s,
        (self.bounds.size.height-720*s)/2+rect.origin.y*s,rect.size.width*s,rect.size.height*s);
}
- (void)setFrameSize:(NSSize)size {[super setFrameSize:size];[self.owner arrange];self.needsDisplay=YES;}
- (void)drawRect:(NSRect)dirty {
    srw64::localization::Scope language(srw64::localization::snapshot());
    box(self.bounds,0x070C13);
    if(!self.owner)return;
    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform* transform=[NSAffineTransform transform];const auto origin=[self place:NSMakeRect(0,0,0,0)].origin;
    [transform translateXBy:origin.x yBy:origin.y];[transform scaleBy:self.scale];[transform concat];
    box(NSMakeRect(0,0,6,720),0x89DBF1);
    draw(@"SRW 64",NSMakeRect(54,36,180,24),18,0xD9F3FA,YES);
    draw(label("link_section"),NSMakeRect(202,39,400,22),13,0x718DA4);
    box(NSMakeRect(54,88,972,1),0x203345);
    draw(label("link_title"),NSMakeRect(54,115,972,47),32,0xEDF5FC,YES);
    draw(label("link_hint"),NSMakeRect(54,169,970,30),14,0x8DA6BA);
    draw(label("link_keyboard_hint"),NSMakeRect(54,676,972,23),12,0x6E879C);
    [NSGraphicsContext restoreGraphicsState];
}
@end

@implementation SRW64LinkController
- (instancetype)init {
    if(!(self=[super init]))return nil;
    page=[[SRW64LinkPage alloc] initWithFrame:game_window.contentView.bounds];page.owner=self;
    page.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;page.wantsLayer=YES;
    page.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    NSMutableArray* list=[NSMutableArray array];
    for(unsigned n=0;n<3;++n) {
        auto* card=[[SRW64LinkCard alloc] init];card.owner=self;card.series=n;card.target=self;card.action=@selector(pick:);
        card.bordered=NO;card.buttonType=NSButtonTypeMomentaryChange;[page addSubview:card];[list addObject:card];
    }
    cards=list;
    auto button=[&](SEL action) {
        auto* value=[[SRW64LinkButton alloc] init];value.target=self;value.action=action;value.bordered=NO;
        value.buttonType=NSButtonTypeMomentaryPushIn;[page addSubview:value];return value;
    };
    back=button(@selector(goBack:));next=button(@selector(commit:));next.primary=YES;next.keyEquivalent=@"\r";
    return self;
}
- (void)arrange {
    for(unsigned n=0;n<3;++n)cards[n].frame=[page place:NSMakeRect(54+n*332,222,308,368)];
    back.frame=[page place:NSMakeRect(646,612,168,48)];
    next.frame=[page place:NSMakeRect(830,612,196,48)];
}
- (bool)locked:(unsigned)series {return request.joined[series] || request.scheduled[series];}
- (void)show:(const srw64::link_page::Request&)value {
    request=value;waiting=false;releasing=false;image_mode=-99;
    focus=3;
    for(unsigned n=0;n<3;++n) {
        ticked[n]=value.scheduled[n] && !value.joined[n];
        if(focus==3 && ![self locked:n])focus=n;
    }
    if(focus==3)focus=0;
    if(!page.superview || page.hidden)previousResponder=game_window.firstResponder;
    if(!page.superview)[game_window.contentView addSubview:page positioned:NSWindowAbove relativeTo:nil];
    page.frame=game_window.contentView.bounds;page.hidden=NO;
    [self refresh];
}
- (void)refresh {
    if(image_mode!=srw64::presentation::image_mode.current()) {
        image_mode=srw64::presentation::image_mode.current();
        for(unsigned n=0;n<3;++n) {
            NSMutableArray* images=[NSMutableArray array];
            for(const auto& path:request.portraits[n]) {
                auto* image=[[NSImage alloc] initWithContentsOfFile:string(path)];
                [images addObject:image?:[[NSImage alloc] initWithSize:NSMakeSize(96,96)]];
            }
            portraits[n]=images;
        }
    }
    for(SRW64LinkCard* card in cards) {
        card.title=label(key("series",card.series));card.enabled=!waiting;card.needsDisplay=YES;
        const unsigned n=card.series;
        card.accessibilityValue=request.joined[n]?label("link_joined"):request.scheduled[n]?label("link_scheduled"):
            ticked[n]?label("link_ticked"):@"";
    }
    back.title=label("link_back");next.title=label("link_confirm");
    back.enabled=next.enabled=!waiting;
    [self arrange];page.needsDisplay=YES;
    // NSButton focus depends on the user's Full Keyboard Access setting; the page
    // takes the keys itself, as the protagonist page does.
    if(!waiting)[game_window makeFirstResponder:page];
}
- (void)move:(int)delta {
    if(waiting)return;
    focus=unsigned(int(focus)+3+delta)%3;
    for(SRW64LinkCard* card in cards)card.needsDisplay=YES;
}
- (void)toggle:(unsigned)series {
    if(waiting || series>2 || [self locked:series])return;
    ticked[series]=!ticked[series];
    [self refresh];
}
- (void)pick:(SRW64LinkCard*)sender {
    if(waiting)return;
    focus=sender.series;[self toggle:sender.series];
    for(SRW64LinkCard* card in cards)card.needsDisplay=YES;
}
- (void)commit:(id)sender {
    if(waiting)return;
    unsigned selection=0;
    for(unsigned n=0;n<3;++n)if(ticked[n] && !request.joined[n])selection|=1u<<n;
    srw64::link_page::answer(request.serial,selection,true);
    waiting=true;[game_window makeFirstResponder:nil];[self refresh];
}
- (void)goBack:(id)sender {
    if(waiting)return;
    srw64::link_page::answer(request.serial,0,false);
    waiting=true;[game_window makeFirstResponder:nil];[self refresh];
}
- (void)drawCard:(unsigned)series bounds:(NSRect)bounds {
    srw64::localization::Scope language(srw64::localization::snapshot());
    // Design size 308 x 368: the lead pilot above, the pilots who come with him in
    // a row below a divider, a tick (or the series' state) in the top right corner.
    const CGFloat s=bounds.size.height/368;
    const bool active=series==focus, joined=request.joined[series], scheduled=request.scheduled[series];
    const bool on=ticked[series] || scheduled;
    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform* transform=[NSAffineTransform transform];[transform scaleBy:s];[transform concat];
    const NSRect card=NSMakeRect(1,1,306,366);
    box(card,active?0x13263A:0x101E2D,14);
    stroke_box(card,active?0x81D6EE:(on && !joined?0x4F93AB:0x294254),14,active?2:1);
    const bool smooth=image_mode==1;
    NSArray<NSImage*>* faces=portraits[series];
    const CGFloat fade=joined?.45:1;
    CGContextSetAlpha(NSGraphicsContext.currentContext.CGContext,fade);
    portrait_in(faces.count?faces[0]:nil,NSMakeRect(24,20,172,172),12,smooth);
    draw(label(key("series",series)),NSMakeRect(24,202,260,18),13,0x8CD6EF,YES);
    draw(label(key("lead",series)),NSMakeRect(24,222,260,34),26,0xF0F6FC,YES);
    draw(label(key("units",series)),NSMakeRect(24,258,260,22),16,0xADC4D6);
    box(NSMakeRect(24,290,260,1),active?0x2E4A60:0x243849);
    CGFloat x=24;
    for(NSUInteger i=1;i<faces.count;++i,x+=64)portrait_in(faces[i],NSMakeRect(x,302,56,56),10,smooth);
    draw(label("link_crew_label"),NSMakeRect(x+4,302,284-x,15),11,0x809DB4);
    draw(label(key("crew",series)),NSMakeRect(x+4,318,284-x,21),15,0xE7F3FC,YES);
    CGContextSetAlpha(NSGraphicsContext.currentContext.CGContext,1);
    // Top right: a tick box for series that can still be chosen, a state label otherwise.
    if(joined || scheduled) {
        NSString* state=label(joined?"link_joined":"link_scheduled");
        const CGFloat width=MIN(92.,[state sizeWithAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12 weight:NSFontWeightSemibold]}].width+22);
        box(NSMakeRect(284-width,22,width,24),joined?0x243849:0x9BE4F7,12);
        draw(state,NSMakeRect(284-width,26,width,18),12,joined?0xADC4D6:0x0A2736,YES,NSTextAlignmentCenter);
    } else {
        const NSRect tick=NSMakeRect(248,20,36,36);
        if(on) {box(tick,0x9BE4F7,10);draw(@"✓",NSMakeRect(248,25,36,26),20,0x0A2736,YES,NSTextAlignmentCenter);}
        else {box(tick,0x122131,10);stroke_box(NSInsetRect(tick,.5,.5),active?0x81D6EE:0x3B5467,10,1.5);}
    }
    [NSGraphicsContext restoreGraphicsState];
}
@end

namespace srw64::link_page {
namespace {SRW64LinkController* controller;}
void window_init(void* cocoa,const std::filesystem::path&) {game_window=(__bridge NSWindow*)cocoa;}
void window_update() {
    if(!game_window)return;
    @autoreleasepool {
        const auto language=localization::snapshot();localization::Scope scope(language);
        static localization::Snapshot previous_language;
        const auto value=request();
        if(value.visible && (!controller || controller->request.serial!=value.serial)) {
            window_claim_input(true);
            if(!controller)controller=[[SRW64LinkController alloc] init];
            [controller show:value];previous_language=language;
        }
        if(!controller)return;
        if(language!=previous_language){previous_language=language;[controller refresh];}
        if(value.visible) {
            if(controller->image_mode!=srw64::presentation::image_mode.current())[controller refresh];
            return;
        }
        if(!controller->page.hidden) {
            controller->page.hidden=YES;[game_window makeFirstResponder:controller->previousResponder];
            controller->releasing=true;
        }
        // The Return, Z or X that closed the page must not reach the game as START,
        // A or B, so input stays here until the game's keys are up, once per close.
        if(!controller->releasing)return;
        const unsigned game_keys[]={0,1,2,6,7,12,13,14,34,37,38,40,36,49,53,76,123,124,125,126};
        bool held=false;
        if(game_window.isKeyWindow)for(auto key:game_keys)held|=CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,key);
        for(auto key:game_keys)held|=srw64::debug::keyboard().holds_mac_key(key);
        window_claim_input(held);
        if(!held)controller->releasing=false;
    }
}
void window_shutdown() {
    if(controller){[controller->page removeFromSuperview];controller=nil;}
    game_window=nil;window_claim_input(false);
}
}
