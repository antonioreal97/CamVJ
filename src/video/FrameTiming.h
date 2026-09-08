#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace atemfx {

// Wall-clock frame timing.
//
// The engine measures two clocks: this one, and GpuTimer. CPU time tells you
// whether the loop is keeping up; GPU time tells you whether the effect chain
// fits the budget. Neither answers the other's question.
class FrameTiming
{
public:
    static constexpr std::size_t kHistorySize = 240;  // 4 seconds at 60 fps

    void reset();

    // Call once at the top of each frame.
    void beginFrame();

    float deltaSeconds() const { return deltaSeconds_; }
    float totalSeconds() const { return totalSeconds_; }

    float lastFrameMs() const { return lastFrameMs_; }
    float averageFrameMs() const { return averageFrameMs_; }
    float maxFrameMs() const { return maxFrameMs_; }
    float fps() const { return averageFrameMs_ > 0.0f ? 1000.0f / averageFrameMs_ : 0.0f; }

    uint64_t frameIndex() const { return frameIndex_; }

    // Ring buffer for plotting. Use with ImGui's values_offset.
    const float* history() const { return history_.data(); }
    std::size_t  historyOffset() const { return historyCursor_; }

private:
    std::array<float, kHistorySize> history_{};
    std::size_t                     historyCursor_ = 0;
    std::size_t                     historyFilled_ = 0;

    double startSeconds_ = 0.0;
    double lastSeconds_  = 0.0;
    bool   started_      = false;

    float    deltaSeconds_   = 0.0f;
    float    totalSeconds_   = 0.0f;
    float    lastFrameMs_    = 0.0f;
    float    averageFrameMs_ = 0.0f;
    float    maxFrameMs_     = 0.0f;
    uint64_t frameIndex_     = 0;
};

} // namespace atemfx
