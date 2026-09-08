#import <Cocoa/Cocoa.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/Log.h"
#include "platform/Display.h"
#include "platform/OutputWindow.h"

// Display enumeration and the borderless output window, AppKit side.
//
// The output window runs inside the main window's event loop: it is never made
// key, never becomes the main window and owns no run loop of its own. That is
// what keeps the operator's keyboard and mouse on the user interface while a
// second display is carrying the program feed.

namespace atemfx {

namespace {

// NSScreen's own number, which is the CGDirectDisplayID.
CGDirectDisplayID screenNumber(NSScreen* screen)
{
    NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
    return number ? static_cast<CGDirectDisplayID>(number.unsignedIntValue) : 0;
}

NSScreen* screenForId(const std::string& id)
{
    for (NSScreen* screen in [NSScreen screens])
    {
        if (std::to_string(screenNumber(screen)) == id)
        {
            return screen;
        }
    }
    return nil;
}

} // namespace

std::vector<DisplayInfo> enumerateDisplays()
{
    std::vector<DisplayInfo> displays;

    @autoreleasepool
    {
        NSArray<NSScreen*>* screens = [NSScreen screens];

        // The first screen is the one carrying the menu bar, not necessarily
        // the one at the coordinate origin.
        NSScreen* primary = screens.count > 0 ? screens[0] : nil;

        for (NSScreen* screen in screens)
        {
            const CGDirectDisplayID number = screenNumber(screen);
            if (number == 0)
            {
                continue;
            }

            DisplayInfo info;
            info.id      = std::to_string(number);
            info.primary = (screen == primary);

            const char* name = [[screen localizedName] UTF8String];
            info.name        = name ? name : "Display";

            // Points times the backing scale, so a Retina panel reports the
            // raster it actually drives rather than its layout size.
            const NSRect  frame = [screen frame];
            const CGFloat scale = [screen backingScaleFactor];
            info.width          = static_cast<uint32_t>(frame.size.width * scale);
            info.height         = static_cast<uint32_t>(frame.size.height * scale);

            CGDisplayModeRef mode = CGDisplayCopyDisplayMode(number);
            if (mode)
            {
                // The mode knows the real pixel count; the screen frame only
                // knows what the window server scaled it to.
                const size_t modeWidth  = CGDisplayModeGetPixelWidth(mode);
                const size_t modeHeight = CGDisplayModeGetPixelHeight(mode);
                if (modeWidth > 0 && modeHeight > 0)
                {
                    info.width  = static_cast<uint32_t>(modeWidth);
                    info.height = static_cast<uint32_t>(modeHeight);
                }
                info.refreshHz = CGDisplayModeGetRefreshRate(mode);
                CGDisplayModeRelease(mode);
            }

            displays.push_back(std::move(info));
        }
    }

    return displays;
}

// ---------------------------------------------------------------------------
// MacOutputWindow
// ---------------------------------------------------------------------------

class MacOutputWindow final : public OutputWindow
{
public:
    ~MacOutputWindow() override { close(); }

    bool open(const std::string& displayId) override;
    void close() override;

    bool  isOpen() const override { return window_ != nil; }
    void* nativeHandle() const override { return (__bridge void*)view_; }

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }

    bool consumeCloseRequest() override
    {
        return closeRequested_ && closeRequested_->exchange(false);
    }

private:
    NSWindow* window_ = nil;
    NSView*   view_   = nil;

    // Escape reaches the application, not this window: the output window is
    // never key. A local monitor sees the key wherever it lands.
    id                                 escapeMonitor_ = nil;
    std::shared_ptr<std::atomic<bool>> closeRequested_;

    // Kept for as long as the output is live, so the display driving the show
    // does not go to sleep behind a static-looking picture.
    id<NSObject> sleepActivity_ = nil;

    // Points, as Window::width() is. The backend multiplies by the layer's
    // contentsScale to get the drawable's pixels.
    uint32_t width_  = 0;
    uint32_t height_ = 0;
};

bool MacOutputWindow::open(const std::string& displayId)
{
    close();

    @autoreleasepool
    {
        NSScreen* screen = screenForId(displayId);
        if (!screen)
        {
            ATEMFX_LOG_ERROR("Display %s is not connected", displayId.c_str());
            return false;
        }

        const NSRect frame = [screen frame];

        window_ = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO
                                                 screen:screen];
        if (!window_)
        {
            ATEMFX_LOG_ERROR("Failed to create the output window");
            return false;
        }

        // Above the menu bar and the Dock: a feed that something else can
        // cover is not a feed.
        window_.level           = NSScreenSaverWindowLevel;
        window_.backgroundColor = [NSColor blackColor];
        window_.opaque          = YES;
        window_.hasShadow       = NO;

        // ARC owns this window. Without this, -close would release it a second
        // time, and the output is opened and closed repeatedly over a show.
        window_.releasedWhenClosed = NO;

        // Clicks belong to whatever is underneath. The operator drives the
        // application window; this one is a signal, not a surface.
        window_.ignoresMouseEvents = YES;

        // Stays put when the operator switches spaces, and never joins the
        // window cycle.
        window_.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                     NSWindowCollectionBehaviorStationary |
                                     NSWindowCollectionBehaviorIgnoresCycle |
                                     NSWindowCollectionBehaviorFullScreenNone;

        [window_ setFrame:frame display:NO];

        view_                  = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0,
                                                                         frame.size.width,
                                                                         frame.size.height)];
        view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        window_.contentView    = view_;

        // orderFront, not makeKeyAndOrderFront: the user interface keeps the
        // keyboard.
        [window_ orderFrontRegardless];

        const NSRect bounds = [view_ bounds];
        width_              = static_cast<uint32_t>(bounds.size.width);
        height_             = static_cast<uint32_t>(bounds.size.height);

        closeRequested_ = std::make_shared<std::atomic<bool>>(false);

        std::shared_ptr<std::atomic<bool>> requested = closeRequested_;
        escapeMonitor_ = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                               handler:^NSEvent*(NSEvent* event) {
            // 53 is Escape. The event is passed on rather than swallowed: the
            // user interface may want it too.
            if (event.keyCode == 53)
            {
                requested->store(true);
            }
            return event;
        }];

        sleepActivity_ = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityIdleDisplaySleepDisabled | NSActivityUserInitiated
                              reason:@"CamVJ is driving a display output"];

        ATEMFX_LOG_INFO("Output window open on display %s: %ux%u points",
                        displayId.c_str(),
                        width_,
                        height_);
        return true;
    }
}

void MacOutputWindow::close()
{
    @autoreleasepool
    {
        if (escapeMonitor_)
        {
            [NSEvent removeMonitor:escapeMonitor_];
            escapeMonitor_ = nil;
        }

        if (sleepActivity_)
        {
            [[NSProcessInfo processInfo] endActivity:sleepActivity_];
            sleepActivity_ = nil;
        }

        if (window_)
        {
            [window_ orderOut:nil];
            [window_ close];
            window_ = nil;
        }

        view_           = nil;
        closeRequested_ = nullptr;
        width_          = 0;
        height_         = 0;
    }
}

std::unique_ptr<OutputWindow> createOutputWindow()
{
    return std::make_unique<MacOutputWindow>();
}

} // namespace atemfx
