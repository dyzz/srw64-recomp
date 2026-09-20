#import <Cocoa/Cocoa.h>
#include "app/desktop.hpp"
#include <stdexcept>

namespace srw64::app {
namespace {
void activate() {
    if (![NSThread isMainThread]) throw std::runtime_error("Desktop dialogs require the main thread");
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
}
NSString* text(const std::string& value) {
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding]
        ?: @"The application could not display the error text.";
}
}
bool macos_choose_another_rom() {
    return ([NSEvent modifierFlags] & NSEventModifierFlagOption) != 0;
}
DesktopUi macos_desktop_ui() {
    return {
        []() -> std::optional<fs::path> {
            @autoreleasepool {
                activate();
                NSOpenPanel* panel = [NSOpenPanel openPanel];
                panel.title = @"SRW64 Recompiled — Select ROM";
                panel.message = @"Select your own unmodified Super Robot Wars 64 (Japan, Rev 0) .z64 ROM.\n"
                                 "The ROM is not included. Importing it does not require Python or build tools.";
                panel.prompt = @"Use ROM";
                panel.canChooseFiles = YES;
                panel.canChooseDirectories = NO;
                panel.allowsMultipleSelection = NO;
                panel.resolvesAliases = YES;
                // Identity is checked by SHA-256, not an extension or file name.
                // Do not install an NSApplication delegate: SDL owns the game.
                if ([panel runModal] != NSModalResponseOK) return std::nullopt;
                NSURL* url = panel.URL;
                if (!url || !url.isFileURL) throw std::runtime_error("No local ROM was selected");
                const char* path = url.fileSystemRepresentation;
                if (!path) throw std::runtime_error("Selected ROM path is unavailable");
                return fs::path(path);
            }
        },
        [](const std::string& message) {
            @autoreleasepool {
                activate();
                NSAlert* alert = [[NSAlert alloc] init];
                alert.alertStyle = NSAlertStyleWarning;
                alert.messageText = @"SRW64 Recompiled";
                alert.informativeText = text(message);
                [alert addButtonWithTitle:@"OK"];
                [alert runModal];
            }
        }
    };
}
}
