#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "decklink/decklink_capture.h"
#include "video/VideoSource.h"

namespace atemfx {

// DeckLink capture as a VideoSource. This is only FX-011's input side: it does
// not own playback, output scheduling, hardware timing or the future M1 frame
// queues. Those stay separate seams in docs/VIDEO_PIPELINE.md.
class DeckLinkSource final : public VideoSource
{
public:
    explicit DeckLinkSource(VideoSourceDescriptor descriptor);
    ~DeckLinkSource() override;

    bool        initialize(EffectContext& context, std::string& error) override;
    void        shutdown() override;
    GpuTexture* render(EffectContext& context) override;
    std::string status() const override;

    void setFrameObserver(FrameObserver observer) override;

private:
    void onFrame(const DeckLinkCaptureFrame& frame);  // capture callback thread

    std::unique_ptr<DeckLinkCapture> capture_;

    // Written once before capture starts and never again, so the capture
    // callback can call it without a lock.
    FrameObserver frameObserver_;

    // Temporary single-slot handoff, matching CameraSource and the M1 policy of
    // latest frame wins. FX-013 replaces this with the bounded queue.
    mutable std::mutex   mutex_;
    std::vector<uint8_t> pending_;
    uint32_t             pendingWidth_ = 0;
    uint32_t             pendingHeight_ = 0;
    std::size_t          pendingRowBytes_ = 0;
    bool                 pendingBottomUp_ = false;
    bool                 hasPending_ = false;
    uint64_t             framesReceived_ = 0;

    std::vector<uint8_t> current_;
    uint32_t             frameWidth_ = 0;
    uint32_t             frameHeight_ = 0;
    std::size_t          frameRowBytes_ = 0;
    bool                 frameBottomUp_ = false;

    GpuTexture* target_ = nullptr;
};

} // namespace atemfx
