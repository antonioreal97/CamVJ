#include "video/source_health.h"

#include <algorithm>

namespace atemfx {

namespace {

constexpr double kRateWindowSeconds = 1.0;

} // namespace

void SourceCaptureMonitor::recordFrame(double arrivalSeconds, bool overwritesPending)
{
    const bool startsWindow = health_.receivedFrames == 0 ||
        arrivalSeconds - health_.lastArrivalSeconds >= kSourceStaleSeconds;

    ++health_.receivedFrames;
    if (overwritesPending)
    {
        ++health_.overwrittenFrames;
    }
    health_.lastArrivalSeconds = arrivalSeconds;

    if (startsWindow)
    {
        // A recovered signal must earn a new rate measurement; the silence
        // before recovery is not part of its current capture cadence.
        windowStartSeconds_ = arrivalSeconds;
        windowIntervals_    = 0;
        health_.captureFps  = 0.0;
        return;
    }

    ++windowIntervals_;
    const double elapsed = arrivalSeconds - windowStartSeconds_;
    if (elapsed > 0.0)
    {
        health_.captureFps = static_cast<double>(windowIntervals_) / elapsed;
    }
    if (elapsed >= kRateWindowSeconds)
    {
        windowStartSeconds_ = arrivalSeconds;
        windowIntervals_    = 0;
    }
}

void SourceHealthMonitor::update(const SourceCaptureHealth& capture,
                                bool consumedFrame,
                                double nowSeconds)
{
    health_.receivedFrames    = capture.receivedFrames;
    health_.overwrittenFrames = capture.overwrittenFrames;

    if (consumedFrame)
    {
        ++health_.consumedFrames;
        consumedArrivalSeconds_ = capture.lastArrivalSeconds;
    }
    else if (health_.consumedFrames != 0)
    {
        ++health_.repeatedFrames;
    }

    if (health_.consumedFrames == 0)
    {
        health_.signal     = SourceSignal::Waiting;
        health_.ageSeconds = 0.0;
        health_.captureFps = 0.0;
        return;
    }

    health_.ageSeconds = std::max(0.0, nowSeconds - consumedArrivalSeconds_);
    health_.signal = health_.ageSeconds >= kSourceStaleSeconds
        ? SourceSignal::Stale : SourceSignal::Live;
    health_.captureFps = health_.signal == SourceSignal::Live ? capture.captureFps : 0.0;
}

} // namespace atemfx
