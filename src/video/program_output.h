#pragma once

#include <cstddef>
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

// A ramp from one picture to another, in the shape every PROGRAM transition
// needs.
//
// Shared rather than copied, because the parts that are easy to get wrong are
// the same wherever a dissolve runs: it must not overshoot when a frame
// arrives late, it must ease at both ends because that is where the eye is
// looking, and reversing it half way has to continue from the picture actually
// on air rather than restart.
class Dissolve
{
public:
    // Long enough to read as deliberate, short enough that an operator asking
    // for a mode gets it before the moment they wanted it for has passed.
    static constexpr float kDefaultSeconds = 0.35f;

    void  setDuration(float seconds);
    float duration() const { return seconds_; }

    // 1 is arrived, 0 is the far end with the whole ramp still to walk.
    void snap(float endpoint);

    // Swaps the ends without moving the picture: what is on air stays on air,
    // and the ramp now walks back the way it came.
    void reverse();

    float advance(float deltaTime);

    // Eased. A linear dissolve starts and stops visibly.
    float amount() const;

    // "Has not arrived", which includes a ramp that starts this frame — a
    // transition must not be mistaken for finished before its first step.
    bool active() const { return phase_ < 1.0f; }

private:
    float phase_   = 1.0f;
    float seconds_ = kDefaultSeconds;
};

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
    static constexpr float kDefaultSeconds = Dissolve::kDefaultSeconds;

    void  setDuration(float seconds) { dissolve_.setDuration(seconds); }
    float duration() const { return dissolve_.duration(); }

    // Jumps to the mode's endpoint, with no dissolve. Start-up only: the first
    // frame of a show must already look the way the operator asked for on the
    // command line, and there is nothing yet to dissolve from.
    void snapTo(ProgramMode mode);

    // Advances toward `mode` and returns the chain's effect mix. Freeze and
    // Black hold the ramp where it stands: the look is not what the audience
    // is watching, and an operator who cut away mid-mix has to come back to
    // the picture they left rather than to one that carried on without them.
    float update(ProgramMode mode, float deltaTime);

    // What the chain is given: 1 the full chain, 0 Clean.
    float amount() const;

    // How far along the ramp is, whichever way it is going. For the readout.
    float progress() const { return dissolve_.amount(); }

    bool active() const { return dissolve_.active(); }

private:
    ProgramMode to_ = ProgramMode::Effects;
    Dissolve    dissolve_;
};

// Which picture PROGRAM is showing. Effects and Clean are one source, not two:
// the difference between them is a chain mix, and dissolving here as well
// would fade the same change twice.
enum class ProgramSource
{
    Live,
    Freeze,
    Black,
    Composite,
};

// Owns a stable PROGRAM image independently of the chain's scratch targets.
// This is an output policy, not an effect: black and hold must also work when
// the source has disappeared and there is no chain input at all.
//
// All four buttons dissolve. Freeze and Black are not the chain's business —
// they replace the picture rather than change it — so their transition lives
// here, between whole images, while FX and Clean are mixed inside the chain.
class ProgramOutput
{
public:
    bool initialize(EffectContext& context, std::string& error);
    void shutdown();

    // Loss latches Freeze after a valid image. Only an explicit operator mode
    // change resumes moving video; a returning camera cannot take itself live.
    GpuTexture* render(EffectContext& context, GpuTexture* frame,
                       bool inputHealthy, ProgramMode& mode);

    // For the operator's readout: how far the picture has travelled toward the
    // mode lit on the buttons, and whether it is still travelling.
    bool  transitioning() const { return dissolve_.active(); }
    float progress() const { return dissolve_.amount(); }

    void  setDuration(float seconds) { dissolve_.setDuration(seconds); }

private:
    GpuTexture* sourceTexture(ProgramSource source) const;
    void        retarget(EffectContext& context, ProgramSource requested, ShaderHandle blit,
                         const EffectConstants& constants, bool canDissolve);
    void        cutTo(ProgramSource source);

    // Two live targets so the frozen still and the moving picture can be on
    // screen at once. While the output is wholly live they alternate and the
    // newest is what Freeze would recall; the moment a transition starts, the
    // latched one is left alone and every further live frame goes to the other.
    GpuTexture* live_[2]     = {nullptr, nullptr};
    GpuTexture* liveCurrent_ = nullptr;
    GpuTexture* held_        = nullptr;
    GpuTexture* black_       = nullptr;
    GpuTexture* blackPixel_  = nullptr;
    GpuTexture* mix_         = nullptr;
    GpuTexture* composite_   = nullptr;
    GpuTexture* presented_   = nullptr;

    std::size_t   writeIndex_ = 0;
    ProgramSource from_       = ProgramSource::Live;
    ProgramSource to_         = ProgramSource::Live;
    Dissolve      dissolve_;

    bool haveFrame_        = false;
    bool blackReady_       = false;
    FramingRect heldFraming_;
    bool heldFramingActive_ = false;
    float heldAspect_       = 16.0f / 9.0f;
};

} // namespace atemfx
