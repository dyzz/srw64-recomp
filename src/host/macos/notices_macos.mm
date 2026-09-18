#import <Cocoa/Cocoa.h>
#include "notices.hpp"
#include <deque>
#include <mutex>
#include <vector>

uint64_t srw64_current_vi();

// A rounded panel in the name page's colours; its label is a plain text field so
// the debug interface can read it. Drawn in drawRect: rather than with layer
// properties, so cacheDisplayInRect: (debug screenshots) renders it as shown.
@interface SRW64NoticeBanner : NSView
@property(nonatomic,strong) NSTextField* label;
@property(nonatomic) CGFloat scale;
@end
@implementation SRW64NoticeBanner
- (void)drawRect:(NSRect)dirty {
    const CGFloat radius=8*self.scale;
    NSBezierPath* path=[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds,.5,.5) xRadius:radius yRadius:radius];
    [[NSColor colorWithSRGBRed:0x12/255. green:0x21/255. blue:0x31/255. alpha:.92] setFill];[path fill];
    [[NSColor colorWithSRGBRed:0x9B/255. green:0xE4/255. blue:0xF7/255. alpha:.85] setStroke];path.lineWidth=1;[path stroke];
}
@end

namespace srw64::notices {
namespace {
constexpr double shown_seconds=6, fade_seconds=.5;
constexpr size_t most_shown=3, most_kept=16;
struct Posted {std::string kind,text;uint64_t vi{};};
struct Banner {SRW64NoticeBanner* view;CFAbsoluteTime until;};
std::mutex mutex;
std::deque<Posted> pending, waiting;   // posted by any thread / not shown yet
std::deque<nlohmann::json> kept;
NSWindow* game_window;
std::vector<Banner> banners;

NSString* ns(const std::string& text){return [NSString stringWithUTF8String:text.c_str()]?:@"";}
// Same design size as the name page: 1080 x 720 points at scale 1.
CGFloat scale_of(NSView* content){return MIN(content.bounds.size.width/1080.,content.bounds.size.height/720.);}
void layout(NSView* content) {
    const CGFloat s=scale_of(content), width=content.bounds.size.width, height=content.bounds.size.height;
    CGFloat top=10*s;
    for(auto& banner:banners) {
        banner.view.scale=s;
        banner.view.label.font=[NSFont systemFontOfSize:15*s weight:NSFontWeightSemibold];
        [banner.view.label sizeToFit];
        // A little slack over the fitted width, or rounding truncates the last glyphs.
        const NSSize text=NSMakeSize(ceil(banner.view.label.frame.size.width)+4,ceil(banner.view.label.frame.size.height));
        const CGFloat w=ceil(MIN(text.width+36*s,width*.92)), h=ceil(text.height+16*s);
        const CGFloat y=content.isFlipped?top:height-top-h;
        banner.view.frame=NSMakeRect(round((width-w)/2),round(y),w,h);
        banner.view.label.frame=NSMakeRect(18*s,(h-text.height)/2,w-36*s,text.height);
        [banner.view setNeedsDisplay:YES];
        top+=h+6*s;
    }
}
}

void window_init(void* cocoa_window){game_window=(__bridge NSWindow*)cocoa_window;}

void post(const std::string& kind,const std::string& text) {
    std::lock_guard lock(mutex);
    pending.push_back({kind,text,srw64_current_vi()});
    kept.push_back({{"kind",kind},{"text",text},{"vi",pending.back().vi}});
    while(kept.size()>most_kept)kept.pop_front();
}

nlohmann::json recent() {
    std::lock_guard lock(mutex);
    return nlohmann::json(kept);
}

void update() {
    {
        std::lock_guard lock(mutex);
        waiting.insert(waiting.end(),pending.begin(),pending.end());
        pending.clear();
    }
    if(!game_window || (waiting.empty() && banners.empty()))return;
    @autoreleasepool {
        NSView* content=game_window.contentView;
        const CFAbsoluteTime now=CFAbsoluteTimeGetCurrent();
        // At most three at once; several removals in one event queue up rather than
        // push each other off early.
        for(;!waiting.empty() && banners.size()<most_shown;waiting.pop_front()) {
            const auto& item=waiting.front();
            SRW64NoticeBanner* view=[[SRW64NoticeBanner alloc] initWithFrame:NSZeroRect];
            NSTextField* label=[NSTextField labelWithString:ns(item.text)];
            label.textColor=[NSColor colorWithSRGBRed:0xF0/255. green:0xF5/255. blue:0xFA/255. alpha:1];
            label.alignment=NSTextAlignmentCenter;
            label.lineBreakMode=NSLineBreakByTruncatingTail;
            view.label=label;[view addSubview:label];
            view.accessibilityLabel=label.stringValue;
            [content addSubview:view positioned:NSWindowAbove relativeTo:nil];
            banners.push_back({view,now+shown_seconds});
        }
        for(auto it=banners.begin();it!=banners.end();) {
            const double left=it->until-now;
            if(left<=0){[it->view removeFromSuperview];it=banners.erase(it);continue;}
            it->view.alphaValue=left<fade_seconds?left/fade_seconds:1;
            ++it;
        }
        layout(content);
    }
}
}
