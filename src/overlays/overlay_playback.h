#pragma once

#include <cstdint>

#include "overlays/overlay_model.h"

namespace atemfx {

// Per-layer clock and visibility envelope. It owns no images and performs no
// work other than bounded arithmetic, so advancing it is safe in the frame
// loop. Decode readiness is supplied by OverlaySystem.
class OverlayPlaybackState
{
public:
    void configure(OverlayKind kind, std::uint32_t frameCount,
                   OverlayPlayback playback, float framesPerSecond);
    void clear();

    void setReady(bool ready);
    void setEnabled(bool enabled);
    void setPlayback(OverlayPlayback playback) { playback_ = playback; }
    void setFramesPerSecond(float framesPerSecond);
    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    // Restarts a one-shot (or loop) at frame zero. If the layer was fading
    // out, the envelope reverses from its current value without a jump.
    void restart();

    // Returns true exactly once when a one-shot has faded all the way out and
    // its persisted enabled flag should be cleared by the owner.
    bool advance(float deltaTime);

    OverlayLayerPhase phase() const { return phase_; }
    float              fadeAmount() const;
    std::uint32_t      desiredFrame() const;
    std::uint32_t      frameCount() const { return frameCount_; }
    bool               enabledRequested() const { return enabledRequested_; }
    bool               ready() const { return ready_; }

private:
    void beginFadeIn();

    OverlayKind       kind_              = OverlayKind::Still;
    OverlayPlayback   playback_          = OverlayPlayback::Loop;
    OverlayLayerPhase phase_             = OverlayLayerPhase::Disabled;
    std::uint32_t     frameCount_         = 0;
    float             framesPerSecond_   = kOverlaySequenceFps;
    double            elapsedSeconds_    = 0.0;
    float             fadeLinear_        = 0.0f;
    bool              enabledRequested_  = false;
    bool              ready_             = false;
    bool              paused_            = false;
    bool              oneShotEnding_     = false;
};

} // namespace atemfx
