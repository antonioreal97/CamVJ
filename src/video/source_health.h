#pragma once

#include <cstdint>

namespace atemfx {

enum class SourceSignal { Generated, Waiting, Live, Stale };

struct SourceHealth
{
    SourceSignal signal = SourceSignal::Generated;
    double ageSeconds   = 0.0;
    double captureFps   = 0.0;
    uint64_t receivedFrames    = 0;
    uint64_t consumedFrames    = 0;
    uint64_t overwrittenFrames = 0;
    uint64_t repeatedFrames    = 0;
};

inline constexpr double kSourceStaleSeconds = 0.5;

// Copied alongside the newest camera buffer under its existing handoff lock.
struct SourceCaptureHealth
{
    uint64_t receivedFrames    = 0;
    uint64_t overwrittenFrames = 0;
    double lastArrivalSeconds  = 0.0;
    double captureFps          = 0.0;
};

// Capture-thread only. Timestamps use one monotonic clock shared with render.
class SourceCaptureMonitor
{
public:
    void recordFrame(double arrivalSeconds, bool overwritesPending);
    SourceCaptureHealth snapshot() const { return health_; }

private:
    SourceCaptureHealth health_;
    double windowStartSeconds_ = 0.0;
    uint64_t windowIntervals_  = 0;
};

// Render-thread only. Repeats count render ticks that reuse a captured frame;
// they are not failed captures or proof that a display presented the frame.
class SourceHealthMonitor
{
public:
    void update(const SourceCaptureHealth& capture, bool consumedFrame, double nowSeconds);
    SourceHealth snapshot() const { return health_; }

private:
    SourceHealth health_{SourceSignal::Waiting};
    double consumedArrivalSeconds_ = 0.0;
};

} // namespace atemfx
