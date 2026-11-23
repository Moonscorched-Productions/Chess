#include "platform/Platform.h"

// SFML 3.0 supports high-DPI rendering on macOS internally but always creates its view with
// the option switched off. This switches it on for an existing window: the OpenGL surface
// gets the full backing resolution and SFML's own scale factor (used for sizes and mouse
// positions) follows the screen.

#define GL_SILENCE_DEPRECATION
#import <AppKit/AppKit.h>

// Methods of SFML's private view class (SFOpenGLView).
@protocol ChessSfmlOpenGLView
- (CGFloat)displayScaleFactor;
- (void)updateScaleFactor;
@end

namespace platform {

namespace {

// The fullscreen window nests SFML's view inside a plain black view.
NSView* findSfmlView(NSView* view) {
    if (view == nil) return nil;
    if ([view isKindOfClass:[NSOpenGLView class]] && [view respondsToSelector:@selector(displayScaleFactor)] &&
        [view respondsToSelector:@selector(updateScaleFactor)]) {
        return view;
    }
    for (NSView* child in [view subviews]) {
        if (NSView* found = findSfmlView(child)) return found;
    }
    return nil;
}

NSView* sfmlView(void* window) {
    if (window == nullptr) return nil;
    return findSfmlView([(__bridge NSWindow*)window contentView]);
}

// Sets SFML's private "m_highDpi" flag through key-value coding. False if the object has no such flag.
bool setHighDpiFlag(id object) {
    if (object == nil) return false;
    @try {
        [object setValue:@YES forKey:@"m_highDpi"];
        return true;
    } @catch (NSException*) {
        return false;
    }
}

}  // namespace

void enableHighDpi(void* window) {
    @autoreleasepool {
        NSView* view = sfmlView(window);
        if (view == nil || !setHighDpiFlag(view)) return;
        // The window controller remembers the flag for views it creates later.
        setHighDpiFlag([(__bridge NSWindow*)window delegate]);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"  // NSView's OpenGL API, which SFML uses
        [view setWantsBestResolutionOpenGLSurface:YES];
#pragma clang diagnostic pop
        // Recomputes SFML's scale factor and sends a Resized event with the size in pixels.
        [static_cast<id<ChessSfmlOpenGLView>>(view) updateScaleFactor];
        [[NSOpenGLContext currentContext] update];
    }
}

float windowScaleFactor(void* window) {
    @autoreleasepool {
        NSView* view = sfmlView(window);
        if (view == nil) return 1.0f;
        const CGFloat scale = [static_cast<id<ChessSfmlOpenGLView>>(view) displayScaleFactor];
        return scale > 0.0 ? static_cast<float>(scale) : 1.0f;
    }
}

}  // namespace platform
