#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace atemfx {

// A borderless window covering one display, showing the processed frame and
// nothing else.
//
// This is the program feed. Downstream — an LED processor, a projector, a
// switcher input — it is indistinguishable from any other video signal, so it
// carries no title bar, no cursor and no user interface. It is deliberately
// *not* a `Window`: it owns no event loop. The main window's loop pumps the
// process's events and both windows live inside it.
class OutputWindow
{
public:
    virtual ~OutputWindow() = default;

    // `displayId` comes from enumerateDisplays(). False if that display is
    // gone, which is the normal answer when a cable was pulled.
    virtual bool open(const std::string& displayId) = 0;
    virtual void close()                            = 0;
    virtual bool isOpen() const                     = 0;

    // NSView* on macOS, HWND on Windows. Only the matching backend reads it.
    virtual void* nativeHandle() const = 0;

    virtual uint32_t width() const  = 0;
    virtual uint32_t height() const = 0;

    // True once, when the operator asked for the output to stop from the
    // output window itself (Escape). The application closes it between frames:
    // the surface rendering into this window has to go first.
    //
    // The escape hatch matters because output can land on the display holding
    // the user interface, and a borderless window above the menu bar is not
    // something you can click your way out of.
    virtual bool consumeCloseRequest() = 0;
};

std::unique_ptr<OutputWindow> createOutputWindow();

} // namespace atemfx
