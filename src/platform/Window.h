#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace atemfx {

// A platform window and its event loop.
//
// The loop is owned by the platform, not by the application: Win32 wants a
// message pump, AppKit wants NSApplication to run and call back. `runFrameLoop`
// hides that difference and hands App the same shape on both.
class Window
{
public:
    using FrameCallback  = std::function<void()>;
    using ResizeCallback = std::function<void(uint32_t, uint32_t)>;

    virtual ~Window() = default;

    virtual bool create(const std::string& title, uint32_t width, uint32_t height) = 0;
    virtual void destroy() = 0;

    // Runs until the window closes, calling `onFrame` once per frame.
    virtual void runFrameLoop(const FrameCallback& onFrame) = 0;

    virtual uint32_t width() const     = 0;
    virtual uint32_t height() const    = 0;
    virtual bool     minimized() const = 0;

    // HWND on Windows, NSView* on macOS. Only the matching backend reads it.
    virtual void* nativeHandle() const = 0;

    void setResizeCallback(ResizeCallback callback) { resizeCallback_ = std::move(callback); }

protected:
    ResizeCallback resizeCallback_;
};

} // namespace atemfx
