#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <thread>

#include "platform/Display.h"

namespace {

int failures = 0;
int checks   = 0;

void expect(bool value, const char* description)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

void notifyScreenChange()
{
    [[NSNotificationCenter defaultCenter]
        postNotificationName:NSApplicationDidChangeScreenParametersNotification object:nil];
}

} // namespace

int main()
{
    // Exercise the actual notification observer without opening an AppKit
    // window, changing display settings or requiring a connected display.
    expect(!atemfx::consumeDisplayChanges(), "uninitialized poll is empty");
    @autoreleasepool
    {
        (void)atemfx::enumerateDisplays();
        (void)atemfx::consumeDisplayChanges();
        [[NSNotificationCenter defaultCenter] postNotificationName:@"Unrelated" object:nil];
        expect(!atemfx::consumeDisplayChanges(), "unrelated notification is ignored");
        notifyScreenChange();
        expect(atemfx::consumeDisplayChanges(), "screen notification is received");
        expect(!atemfx::consumeDisplayChanges(), "notification is consumed once");
        notifyScreenChange();
        notifyScreenChange();
        expect(atemfx::consumeDisplayChanges(), "burst of notifications is coalesced");
        expect(!atemfx::consumeDisplayChanges(), "burst does not leave a second pending event");
        notifyScreenChange();
        (void)atemfx::enumerateDisplays();
        expect(atemfx::consumeDisplayChanges(), "enumeration does not discard a pending change");
    }

    @autoreleasepool
    {
        notifyScreenChange();
        expect(atemfx::consumeDisplayChanges(),
               "observer survives autorelease pool and no output window");
    }

    std::thread sender([] {
        @autoreleasepool
        {
            notifyScreenChange();
        }
    });
    sender.join();
    expect(atemfx::consumeDisplayChanges(), "callback can arrive outside polling thread");
    expect(!atemfx::consumeDisplayChanges(), "final poll is empty");
    std::printf("display changes: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
