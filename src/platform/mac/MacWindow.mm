#import <Cocoa/Cocoa.h>

#include <memory>

#include "core/Log.h"
#include "gpu/Backend.h"
#include "platform/Window.h"

// AppKit window and frame loop.
//
// The loop pumps events manually rather than calling -[NSApplication run].
// Frame pacing comes from the Metal layer's next drawable, so the application
// needs to own the cadence; handing it to AppKit's run loop would mean pacing
// the video engine from a timer, which is exactly what a live tool must not do.

namespace atemfx {
class MacWindow;
}

@interface AtemFxWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) atemfx::MacWindow* owner;
@end

namespace atemfx {

class MacWindow final : public Window
{
public:
    ~MacWindow() override { destroy(); }

    bool create(const std::string& title, uint32_t width, uint32_t height) override;
    void destroy() override;
    void runFrameLoop(const FrameCallback& onFrame) override;

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }
    bool     minimized() const override { return minimized_; }
    void*    nativeHandle() const override { return (__bridge void*)view_; }

    // Called from the delegate.
    void onClose() { closed_ = true; }
    void onResize();
    void onMiniaturize(bool minimized) { minimized_ = minimized; }

private:
    NSWindow*               window_   = nil;
    NSView*                 view_     = nil;
    AtemFxWindowDelegate*   delegate_ = nil;

    uint32_t width_     = 0;
    uint32_t height_    = 0;
    bool     minimized_ = false;
    bool     closed_    = false;
};

bool MacWindow::create(const std::string& title, uint32_t width, uint32_t height)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        // Regular, not Accessory: launched from a terminal without an
        // application bundle the process would otherwise get no menu bar and
        // no keyboard focus.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        const NSRect frame = NSMakeRect(0.0, 0.0, width, height);

        const NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                        NSWindowStyleMaskClosable |
                                        NSWindowStyleMaskMiniaturizable |
                                        NSWindowStyleMaskResizable;

        window_ = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        if (!window_)
        {
            ATEMFX_LOG_ERROR("Failed to create the NSWindow");
            return false;
        }

        [window_ setTitle:@(title.c_str())];
        [window_ setContentMinSize:NSMakeSize(960.0, 600.0)];
        [window_ center];

        view_ = [[NSView alloc] initWithFrame:frame];
        view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        window_.contentView    = view_;

        delegate_       = [[AtemFxWindowDelegate alloc] init];
        delegate_.owner = this;
        window_.delegate = delegate_;

        [window_ makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        const NSRect content = [view_ bounds];
        width_               = static_cast<uint32_t>(content.size.width);
        height_              = static_cast<uint32_t>(content.size.height);

        ATEMFX_LOG_INFO("Window created: %ux%u", width_, height_);
        return true;
    }
}

void MacWindow::destroy()
{
    @autoreleasepool
    {
        if (window_)
        {
            window_.delegate = nil;
            [window_ close];
            window_ = nil;
        }
        delegate_ = nil;
        view_     = nil;
        closed_   = true;
    }
}

void MacWindow::onResize()
{
    if (!view_)
    {
        return;
    }

    const NSRect content = [view_ bounds];
    width_               = static_cast<uint32_t>(content.size.width);
    height_              = static_cast<uint32_t>(content.size.height);

    if (width_ > 0 && height_ > 0 && resizeCallback_)
    {
        resizeCallback_(width_, height_);
    }
}

void MacWindow::runFrameLoop(const FrameCallback& onFrame)
{
    [NSApp finishLaunching];

    while (!closed_)
    {
        @autoreleasepool
        {
            // Drain everything queued, then render once. Local event monitors
            // — which is how ImGui's macOS backend sees input — run inside
            // -[NSApplication sendEvent:].
            for (;;)
            {
                NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                    untilDate:nil
                                                       inMode:NSDefaultRunLoopMode
                                                      dequeue:YES];
                if (!event)
                {
                    break;
                }
                [NSApp sendEvent:event];
            }

            if (closed_)
            {
                break;
            }

            onFrame();
        }
    }
}

std::unique_ptr<Window> createPlatformWindow()
{
    return std::make_unique<MacWindow>();
}

} // namespace atemfx

@implementation AtemFxWindowDelegate

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    if (self.owner)
    {
        self.owner->onClose();
    }
}

- (void)windowDidResize:(NSNotification*)notification
{
    (void)notification;
    if (self.owner)
    {
        self.owner->onResize();
    }
}

- (void)windowDidMiniaturize:(NSNotification*)notification
{
    (void)notification;
    if (self.owner)
    {
        self.owner->onMiniaturize(true);
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification
{
    (void)notification;
    if (self.owner)
    {
        self.owner->onMiniaturize(false);
    }
}

@end
