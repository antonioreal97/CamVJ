#pragma once

#include <string>

#include "effects/Effect.h"

namespace atemfx {

enum class ProgramMode
{
    Effects,
    Clean,
    Freeze,
    Black,
};

const char* programModeName(ProgramMode mode);

// The dissolve between the effect chain and Clean.
//
// Pulling a look off PROGRAM in a single frame is a cut, and an audience reads
// an unannounced cut as a fault. So Clean is not a switch that removes the
// visual chain: it is the end of a ramp the chain follows, and this holds the
// ramp's position. Like ProgramOutput this is output policy — the chain is
// told an amount and never learns that a transition exists.
class ProgramTransition
{
public:
    // Long enough to read as deliberate, short enough that an operator asking
    // for Clean gets it before the moment they wanted it for has passed.
    static constexpr float kDefaultSeconds = 0.35f;

    void  setDuration(float seconds);
    float duration() const { return seconds_; }

    // Jumps to the mode's endpoint, with no dissolve. Start-up only: the first
    // frame of a show must already look the way the operator asked for on the
    // command line, and there is nothing yet to dissolve from.
    void snapTo(ProgramMode mode);

    // Advances toward `mode` and returns the chain's effect mix. Freeze and
    // Black hold the ramp where it stands: nothing that moves is on air to
    // dissolve, and an operator who cut away mid-mix has to come back to the
    // picture they left rather than to one that carried on without them.
    float update(ProgramMode mode, float deltaTime);

    // Eased, which is what the chain is given: a linear dissolve starts and
    // stops visibly, and the ends are where the eye is looking.
    float amount() const;

    bool active() const { return phase_ > 0.0f && phase_ < 1.0f; }

private:
    float phase_   = 1.0f;   // 1 the full chain, 0 Clean
    float seconds_ = kDefaultSeconds;
};

// Owns a stable PROGRAM image independently of the chain's scratch targets.
// This is an output policy, not an effect: black and hold must also work when
// the source has disappeared and there is no chain input at all.
class ProgramOutput
{
public:
    bool initialize(EffectContext& context, std::string& error);
    void shutdown();

    // Loss latches Freeze after a valid image. Only an explicit operator mode
    // change resumes moving video; a returning camera cannot take itself live.
    GpuTexture* render(EffectContext& context, GpuTexture* frame,
                       bool inputHealthy, ProgramMode& mode);

private:
    GpuTexture* held_       = nullptr;
    GpuTexture* black_      = nullptr;
    GpuTexture* blackPixel_ = nullptr;
    bool haveFrame_        = false;
    bool blackReady_       = false;
    FramingRect heldFraming_;
    bool heldFramingActive_ = false;
    float heldAspect_       = 16.0f / 9.0f;
};

} // namespace atemfx
