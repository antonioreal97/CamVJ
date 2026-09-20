#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "tracking/FacesSnapshot.h"

namespace atemfx {

// Finds every face in captured frames and keeps an identity on each.
//
// Control plane, exactly like Tracker (see Tracker.h): it reads the frames
// capture already produced in system memory, on its own thread, publishes a
// small fixed-size struct, and can fall behind without costing a frame. It is
// a separate sensor from the subject tracker because the two answer different
// questions — who the camera follows, and who is in the crowd — and one must
// never steer the other.
//
// Implementations: Vision on macOS (src/tracking/mac/VisionFaceSensor.mm).
// Windows has none yet, for the same reason as the tracker (docs/TRACKING.md).
class FaceSensor
{
public:
    virtual ~FaceSensor() = default;

    FaceSensor(const FaceSensor&)            = delete;
    FaceSensor& operator=(const FaceSensor&) = delete;

    virtual bool start(std::string& error) = 0;
    virtual void stop()                    = 0;

    // Render thread, between frames. Inactive means no effect wants faces:
    // submit() returns without copying and the worker sleeps. Deactivating
    // drops every track, so the next activation starts with fresh ids.
    virtual void setActive(bool active) = 0;

    // Drops every track. Called when the input changes: a face on one camera
    // is not the same face on the next.
    virtual void reset() = 0;

    // Capture thread. Same contract as Tracker::submit.
    virtual void submit(const uint8_t* bgra,
                        uint32_t       width,
                        uint32_t       height,
                        std::size_t    rowBytes,
                        bool           bottomUp) = 0;

    // Render thread, once per frame. Capture-space coordinates; App maps them
    // onto the canvas. False before the first cycle or while inactive.
    virtual bool latest(FacesSnapshot& snapshot) const = 0;

    virtual std::string status() const = 0;

protected:
    FaceSensor() = default;
};

// The platform's face sensor, or nullptr where there is none.
std::unique_ptr<FaceSensor> createFaceSensor();

} // namespace atemfx
