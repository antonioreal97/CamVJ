#include "video/FrameTiming.h"

#include <algorithm>
#include <chrono>

namespace atemfx {

namespace {

using Clock = std::chrono::steady_clock;

double nowSeconds()
{
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

} // namespace

void FrameTiming::reset()
{
    const double now = nowSeconds();
    startSeconds_    = now;
    lastSeconds_     = now;

    history_.fill(0.0f);
    historyCursor_  = 0;
    historyFilled_  = 0;
    deltaSeconds_   = 0.0f;
    totalSeconds_   = 0.0f;
    lastFrameMs_    = 0.0f;
    averageFrameMs_ = 0.0f;
    maxFrameMs_     = 0.0f;
    frameIndex_     = 0;
    started_        = true;
}

void FrameTiming::beginFrame()
{
    if (!started_)
    {
        reset();
        return;
    }

    const double now = nowSeconds();

    deltaSeconds_ = static_cast<float>(now - lastSeconds_);
    totalSeconds_ = static_cast<float>(now - startSeconds_);
    lastSeconds_  = now;
    lastFrameMs_  = deltaSeconds_ * 1000.0f;

    // The very first frame has no previous timestamp; a zero would sit in the
    // averaging window for four seconds.
    if (lastFrameMs_ > 0.0f)
    {
        history_[historyCursor_] = lastFrameMs_;
        historyCursor_           = (historyCursor_ + 1) % kHistorySize;
        historyFilled_           = std::min(historyFilled_ + 1, kHistorySize);
    }

    // Recomputed rather than accumulated: 240 floats is nothing, and a running
    // sum drifts and cannot report the window maximum, which is the number
    // that actually matters for dropped frames.
    float sum     = 0.0f;
    float maximum = 0.0f;
    for (std::size_t i = 0; i < historyFilled_; ++i)
    {
        sum     += history_[i];
        maximum  = std::max(maximum, history_[i]);
    }

    averageFrameMs_ = historyFilled_ > 0 ? sum / static_cast<float>(historyFilled_) : 0.0f;
    maxFrameMs_     = maximum;

    ++frameIndex_;
}

} // namespace atemfx
