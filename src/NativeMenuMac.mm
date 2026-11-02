#include "NativeMenuPlatform.h"

#import <Cocoa/Cocoa.h>

#include <functional>

@interface NativeMenuTarget : NSObject {
@public
    const std::function<void(int)>* callback;
}
- (void)activate:(id)sender;
@end

@implementation NativeMenuTarget
- (void)activate:(id)sender {
    callback([sender tag]);
}
@end

namespace {
NSString* toNSString(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

NSMenu* createMenu(
    const std::vector<NativeMenuItem>& items,
    NativeMenuTarget* target
) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    for (const NativeMenuItem& item : items) {
        if (item.separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }

        NSMenuItem* menuItem = [[NSMenuItem alloc]
            initWithTitle:toNSString(item.label)
            action:item.command != 0 ? @selector(activate:) : nil
            keyEquivalent:@""];
        [menuItem setEnabled:item.enabled];
        if (item.checkable) {
            [menuItem setState:item.checked ? NSControlStateValueOn : NSControlStateValueOff];
        }
        if (item.command != 0) {
            [menuItem setTarget:target];
            [menuItem setTag:item.command];
        }
        if (!item.children.empty()) {
            NSMenu* submenu = createMenu(item.children, target);
            [menuItem setSubmenu:submenu];
            [submenu release];
        }
        [menu addItem:menuItem];
        [menuItem release];
    }
    return menu;
}
}

bool showPlatformMenu(
    SDL_Window* parent,
    int x,
    int y,
    const std::vector<NativeMenuItem>& items,
    const std::function<void(int)>& activate
) {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(parent);
    NSWindow* window = (NSWindow*)(
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr)
    );
    if (!window) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not access the native macOS window handle: %s",
            SDL_GetError()
        );
        return false;
    }

    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NativeMenuTarget* target = [[NativeMenuTarget alloc] init];
    target->callback = &activate;
    NSMenu* menu = createMenu(items, target);

    NSView* contentView = [window contentView];
    const CGFloat viewHeight = [contentView bounds].size.height;
    [menu popUpMenuPositioningItem:nil
                       atLocation:NSMakePoint(x, viewHeight - y)
                           inView:contentView];

    [menu release];
    [target release];
    [pool drain];
    return true;
}
