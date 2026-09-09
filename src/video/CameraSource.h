#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "video/CameraCapture.h"
#include "video/VideoSource.h"

namespace atemfx {

// A camera as a video source: platform capture on one side, a GPU texture at
// project resolution on the other.
//
// Everything here is platform-neutral. The only thing the operating system
// contributes is a buffer of BGRA pixels; scaling, aspect handling and mirroring
// all happen on the GPU, because AGENTS.md says so and because a CPU resize of
// 1080p per frame would eat the budget on its own.
class CameraSource final : public VideoSource
{
public:
    explicit CameraSource(VideoSourceDescriptor descriptor);
    ~CameraSource() override;

    bool        initialize(EffectContext& context, std::string& error) override;
    void        shutdown() override;
    GpuTexture* render(EffectContext& context) override;
    std::string status() const override;
    SourceHealth health() const override { return healthMonitor_.snapshot(); }

    void          setFrameObserver(FrameObserver observer) override;
    SourceMapping mapping() const override;

private:
    void onFrame(const CameraFrame& frame);  // capture thread

    std::unique_ptr<CameraCapture> capture_;

    // Written once before capture starts and never again, so the capture
    // thread can call it without a lock.
    FrameObserver frameObserver_;

    // Latest frame wins. A live tool would rather show the newest picture than
    // work through a queue of stale ones, and this is the same policy the
    // bounded frame queue will use for SDI in M1.
    mutable std::mutex   mutex_;
    std::vector<uint8_t> pending_;
    uint32_t             pendingWidth_    = 0;
    uint32_t             pendingHeight_   = 0;
    std::size_t          pendingRowBytes_ = 0;
    bool                 pendingBottomUp_ = false;
    bool                 hasPending_      = false;
    SourceCaptureMonitor captureMonitor_;

    // Render-thread copy, swapped with pending_ so neither side allocates once
    // the frame size settles.
    std::vector<uint8_t> current_;
    uint32_t             frameWidth_    = 0;
    uint32_t             frameHeight_   = 0;
    std::size_t          frameRowBytes_ = 0;
    bool                 frameBottomUp_ = false;
    SourceHealthMonitor  healthMonitor_;

    // The canvas shape the last render used, for mapping(). The canvas is
    // fixed by the project format, but the source is told rather than assuming.
    float canvasAspect_ = 16.0f / 9.0f;

    GpuTexture* target_ = nullptr;
};

} // namespace atemfx
