#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "video/VideoSource.h"

namespace atemfx {

// One captured frame, 8-bit BGRA, top row first. The pixels are valid only for
// the duration of the callback.
struct CameraFrame
{
    const uint8_t* pixels   = nullptr;
    uint32_t       width    = 0;
    uint32_t       height   = 0;
    std::size_t    rowBytes = 0;

    // Some capture stacks hand over bottom-up images — Media Foundation's
    // RGB32 does. The flip happens in the blit shader; reversing rows on the
    // CPU would cost a full frame copy for nothing.
    bool bottomUp = false;
};

using CameraFrameCallback = std::function<void(const CameraFrame&)>;

// Platform camera capture: AVFoundation on macOS, Media Foundation on Windows.
//
// Frames arrive on the platform's own thread. The callback must not block and
// must not touch the GPU — it copies and returns.
class CameraCapture
{
public:
    virtual ~CameraCapture() = default;

    // Opening a camera is the action that triggers the operating system's
    // permission prompt. Returning true means "accepted, opening"; it does not
    // mean frames are flowing yet. Watch status().
    virtual bool start(const std::string&  deviceId,
                       CameraFrameCallback onFrame,
                       std::string&        error) = 0;

    virtual void stop() = 0;

    // Called once per frame from the render thread, so deferred work — a
    // permission grant that arrived asynchronously — has somewhere to land
    // without the capture backend dispatching onto the frame loop.
    virtual void poll() = 0;

    // Empty while the device is healthy; otherwise why there is no picture.
    virtual std::string status() const = 0;
};

std::vector<VideoSourceDescriptor> enumerateCameras();
std::unique_ptr<CameraCapture>     createCameraCapture();

// True when a camera appeared or disappeared since the last call. The frame
// loop polls this between frames and re-runs enumerateCameras(); the flag
// itself is an atomic, not a lock, and never opens a device.
bool consumeCameraHotplug();

} // namespace atemfx
