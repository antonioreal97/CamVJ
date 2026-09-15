#include "overlays/overlay_playback.h"

#include <algorithm>
#include <cmath>

namespace atemfx {

namespace {

constexpr float kMinFramesPerSecond = 1.0f;

float smoothstep(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

} // namespace

void OverlayPlaybackState::configure(OverlayKind kind, std::uint32_t frameCount,
                                     OverlayPlayback playback, float framesPerSecond)
{
    kind_            = kind;
    frameCount_      = frameCount;
    playback_        = playback;
    framesPerSecond_ = std::clamp(std::isfinite(framesPerSecond) ? framesPerSecond
                                                                 : kOverlaySequenceFps,
                                  kMinFramesPerSecond, kOverlaySequenceFps);
    clear();
}

void OverlayPlaybackState::clear()
{
    phase_            = OverlayLayerPhase::Disabled;
    elapsedSeconds_   = 0.0;
    fadeLinear_       = 0.0f;
    enabledRequested_ = false;
    ready_            = false;
    paused_           = false;
    oneShotEnding_    = false;
}

void OverlayPlaybackState::beginFadeIn()
{
    if (phase_ != OverlayLayerPhase::Live)
    {
        phase_ = OverlayLayerPhase::FadingIn;
    }
}

void OverlayPlaybackState::setReady(bool ready)
{
    ready_ = ready;
    if (!ready_)
    {
        if (enabledRequested_ && fadeLinear_ <= 0.0f)
        {
            phase_ = OverlayLayerPhase::Buffering;
        }
        return;
    }

    if (enabledRequested_ &&
        (phase_ == OverlayLayerPhase::Buffering || phase_ == OverlayLayerPhase::Disabled))
    {
        elapsedSeconds_ = 0.0;
        beginFadeIn();
    }
}

void OverlayPlaybackState::setEnabled(bool enabled)
{
    enabledRequested_ = enabled;
    oneShotEnding_     = false;

    if (enabled)
    {
        if (ready_)
        {
            beginFadeIn();
        }
        else
        {
            phase_ = OverlayLayerPhase::Buffering;
        }
        return;
    }

    if (fadeLinear_ > 0.0f)
    {
        phase_ = OverlayLayerPhase::FadingOut;
    }
    else
    {
        phase_ = OverlayLayerPhase::Disabled;
    }
}

void OverlayPlaybackState::setFramesPerSecond(float framesPerSecond)
{
    framesPerSecond_ = std::clamp(std::isfinite(framesPerSecond) ? framesPerSecond
                                                                 : kOverlaySequenceFps,
                                  kMinFramesPerSecond, kOverlaySequenceFps);
}

void OverlayPlaybackState::restart()
{
    elapsedSeconds_   = 0.0;
    enabledRequested_ = true;
    oneShotEnding_    = false;
    if (ready_)
    {
        beginFadeIn();
    }
    else
    {
        phase_ = OverlayLayerPhase::Buffering;
    }
}

bool OverlayPlaybackState::advance(float deltaTime)
{
    const float delta = std::isfinite(deltaTime) ? std::max(deltaTime, 0.0f) : 0.0f;

    if (!paused_ && (phase_ == OverlayLayerPhase::FadingIn ||
                     phase_ == OverlayLayerPhase::Live ||
                     phase_ == OverlayLayerPhase::FadingOut))
    {
        elapsedSeconds_ += static_cast<double>(delta);
    }

    const float fadeStep = delta / kOverlayFadeSeconds;
    if (phase_ == OverlayLayerPhase::FadingIn)
    {
        fadeLinear_ = std::min(fadeLinear_ + fadeStep, 1.0f);
        if (fadeLinear_ >= 1.0f)
        {
            phase_ = OverlayLayerPhase::Live;
        }
    }
    else if (phase_ == OverlayLayerPhase::FadingOut)
    {
        fadeLinear_ = std::max(fadeLinear_ - fadeStep, 0.0f);
        if (fadeLinear_ <= 0.0f)
        {
            phase_ = OverlayLayerPhase::Disabled;
            const bool autoDisabled = oneShotEnding_;
            oneShotEnding_ = false;
            return autoDisabled;
        }
    }

    // Let the visibility envelope consume this tick before starting the
    // automatic one-shot fade. Otherwise a large frame delta can both begin
    // and finish the fade-out in one call, skipping the held final frame.
    if (kind_ == OverlayKind::PngSequence && playback_ == OverlayPlayback::OneShot &&
        enabledRequested_ && frameCount_ > 0)
    {
        const double duration = static_cast<double>(frameCount_) /
                                static_cast<double>(framesPerSecond_);
        if (elapsedSeconds_ >= duration)
        {
            elapsedSeconds_   = duration;
            enabledRequested_ = false;
            oneShotEnding_    = true;
            phase_            = OverlayLayerPhase::FadingOut;
        }
    }

    return false;
}

float OverlayPlaybackState::fadeAmount() const
{
    return smoothstep(fadeLinear_);
}

std::uint32_t OverlayPlaybackState::desiredFrame() const
{
    if (kind_ == OverlayKind::Still || frameCount_ <= 1)
    {
        return 0;
    }

    const double raw = std::floor(elapsedSeconds_ * static_cast<double>(framesPerSecond_));
    const auto ordinal = raw > 0.0 ? static_cast<std::uint64_t>(raw) : std::uint64_t{0};
    if (playback_ == OverlayPlayback::OneShot)
    {
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(ordinal, frameCount_ - 1));
    }
    return static_cast<std::uint32_t>(ordinal % frameCount_);
}

} // namespace atemfx
