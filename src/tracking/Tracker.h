#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "tracking/TrackingSnapshot.h"

namespace atemfx {

// Finds the subject in captured frames.
//
// Tracking is control plane, not pixel pipeline (docs/ARCHITECTURE.md section
// 1). It reads the frames capture already produced in system memory, on its
// own thread, and publishes one small struct; it never touches the GPU, never
// reads back from it, and can fall arbitrarily far behind without costing a
// frame. If detection stops entirely the picture keeps running and the framing
// eases back out — that is the whole reason this sits beside the pipeline
// rather than inside it.
//
// Implementations: Vision on macOS (src/tracking/mac/VisionTracker.mm).
// Windows has none yet — see docs/TRACKING.md.
class Tracker
{
public:
    virtual ~Tracker() = default;

    Tracker(const Tracker&)            = delete;
    Tracker& operator=(const Tracker&) = delete;

    // Starts the worker. False means no tracking is available and why.
    virtual bool start(std::string& error) = 0;
    virtual void stop()                    = 0;

    // Capture thread. Takes a copy of the newest frame and returns; the worker
    // picks up whatever is there when it is ready, so a slow detector drops
    // frames instead of holding up capture. The pixels are only valid for the
    // duration of the call.
    virtual void submit(const uint8_t* bgra,
                        uint32_t       width,
                        uint32_t       height,
                        std::size_t    rowBytes,
                        bool           bottomUp) = 0;

    // Render thread, once per frame. False before the first detection cycle
    // has finished. Coordinates are in the captured image's own normalized
    // space, origin top left; App maps them onto the canvas.
    virtual bool latest(TrackingSnapshot& snapshot) const = 0;

    // One line for the UI: what the tracker is doing, or why it is not.
    virtual std::string status() const = 0;

protected:
    Tracker() = default;
};

// The platform's tracker, or nullptr where there is none.
std::unique_ptr<Tracker> createSubjectTracker();

} // namespace atemfx
