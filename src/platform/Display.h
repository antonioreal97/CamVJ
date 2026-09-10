#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace atemfx {

// A display the operating system can put a window on.
//
// This is the output equivalent of VideoSourceDescriptor: the platform reports
// what exists, the application picks one by id, and nothing above this header
// knows what a CGDirectDisplayID or an HMONITOR is.
struct DisplayInfo
{
    // Stable for as long as the display stays connected. What --output takes.
    std::string id;

    // What the operator reads in the panel.
    std::string name;

    // Native pixels, not points: an LED processor is fed a raster, and a
    // Retina panel reporting 1728 points is really 3456 pixels wide.
    uint32_t width  = 0;
    uint32_t height = 0;

    // Logical desktop geometry used to invalidate an output route when its
    // window would move or change backing scale. The origin is platform-defined;
    // compare snapshots on the same platform rather than converting coordinates.
    int32_t  desktopX      = 0;
    int32_t  desktopY      = 0;
    uint32_t desktopWidth  = 0;
    uint32_t desktopHeight = 0;
    float    scaleFactor   = 1.0f;

    // Zero when the platform does not report one.
    double refreshHz = 0.0;

    // The display carrying the menu bar. Sending output there covers the
    // application's own window.
    bool primary = false;
};

// Never opens a window and never changes display configuration.
std::vector<DisplayInfo> enumerateDisplays();

// True once after the OS reports a display attachment, removal or mode change.
// Poll after the initial enumeration. This only consumes the pending flag and
// never allocates, logs, enumerates or opens a window. Call between frames,
// including while the operator window is hidden, then rebuild the display list.
bool consumeDisplayChanges();

} // namespace atemfx
