#include "video/program_output.h"

#include <algorithm>
#include <cstdint>

#include "core/Log.h"

namespace atemfx {

namespace {

constexpr float kMinDissolveSeconds = 0.01f;

// Only Effects and Clean are ends of the chain mix. Freeze and Black are not
// somewhere between them: they replace the picture outright, so the ramp has
// no target to move toward and stays where the operator left it.
bool isMixEnd(ProgramMode mode)
{
    return mode == ProgramMode::Effects || mode == ProgramMode::Clean;
}

ProgramSource sourceFor(ProgramMode mode)
{
    switch (mode)
    {
    case ProgramMode::Effects:
    case ProgramMode::Clean:   return ProgramSource::Live;
    case ProgramMode::Freeze:  return ProgramSource::Freeze;
    case ProgramMode::Black:   return ProgramSource::Black;
    }
    return ProgramSource::Black;
}

} // namespace

void Dissolve::setDuration(float seconds)
{
    seconds_ = std::max(seconds, kMinDissolveSeconds);
}

void Dissolve::snap(float endpoint)
{
    phase_ = std::clamp(endpoint, 0.0f, 1.0f);
}

void Dissolve::reverse()
{
    phase_ = 1.0f - std::clamp(phase_, 0.0f, 1.0f);
}

float Dissolve::advance(float deltaTime)
{
    if (deltaTime > 0.0f)
    {
        // A frame long enough to cross the whole ramp lands on the end rather
        // than overshooting it: a dropped frame must not make the mix bounce.
        phase_ = std::min(phase_ + deltaTime / seconds_, 1.0f);
    }
    return amount();
}

float Dissolve::amount() const
{
    const float phase = std::clamp(phase_, 0.0f, 1.0f);
    return phase * phase * (3.0f - 2.0f * phase);
}

void ProgramTransition::snapTo(ProgramMode mode)
{
    if (isMixEnd(mode))
    {
        to_ = mode;
        dissolve_.snap(1.0f);
    }
}

float ProgramTransition::update(ProgramMode mode, float deltaTime)
{
    if (!isMixEnd(mode))
    {
        return amount();
    }

    if (mode != to_)
    {
        // Turning round mid-mix keeps the picture that is on air and walks the
        // ramp back, rather than restarting from a look nobody is watching.
        to_ = mode;
        dissolve_.reverse();
    }

    dissolve_.advance(deltaTime);
    return amount();
}

float ProgramTransition::amount() const
{
    const float arrived = dissolve_.amount();
    return to_ == ProgramMode::Clean ? 1.0f - arrived : arrived;
}

const char* programModeName(ProgramMode mode)
{
    switch (mode)
    {
    case ProgramMode::Effects: return "FX";
    case ProgramMode::Clean:   return "CLEAN";
    case ProgramMode::Freeze:  return "FREEZE";
    case ProgramMode::Black:   return "BLACK";
    }
    return "BLACK";
}

bool ProgramOutput::initialize(EffectContext& context, std::string& error)
{
    shutdown();
    if (!context.targets || !context.shaders || !context.fullscreen)
    {
        error = "Program output needs rendering resources";
        return false;
    }
    if (!context.shaders->shader("passthrough", &error))
    {
        return false;
    }

    // Reserved before the show, not on the frame a button is pressed: the
    // first Freeze of the night must not be the one that allocates.
    live_[0]    = &context.targets->persistent("program.live.a");
    live_[1]    = &context.targets->persistent("program.live.b");
    black_      = &context.targets->persistent("program.black");
    mix_        = &context.targets->persistent("program.mix");
    composite_  = &context.targets->persistent("program.composite");
    blackPixel_ = &context.targets->uploadTarget("program.pixel", 1, 1);
    const uint8_t pixel[] = {0, 0, 0, 255};
    if (!live_[0]->valid() || !live_[1]->valid() || !black_->valid() || !mix_->valid() ||
        !composite_->valid() || !blackPixel_->valid() ||
        !context.targets->upload(*blackPixel_, pixel, sizeof(pixel)))
    {
        error = "Failed to prepare program safety images";
        shutdown();
        return false;
    }

    // A broken dissolve costs the transition, never the button: the modes
    // still work, they just cut, which is what they did before.
    std::string mixError;
    if (!context.shaders->shader("crossfade", &mixError))
    {
        ATEMFX_LOG_ERROR("PROGRAM transitions unavailable, modes will cut: %s", mixError.c_str());
    }
    return true;
}

GpuTexture* ProgramOutput::sourceTexture(ProgramSource source) const
{
    switch (source)
    {
    case ProgramSource::Live:      return liveCurrent_;
    case ProgramSource::Freeze:    return held_;
    case ProgramSource::Black:     return black_;
    case ProgramSource::Composite: return composite_;
    }
    return black_;
}

void ProgramOutput::cutTo(ProgramSource source)
{
    from_ = to_ = source;
    dissolve_.snap(1.0f);
}

void ProgramOutput::retarget(EffectContext& context, ProgramSource requested, ShaderHandle blit,
                             const EffectConstants& constants, bool canDissolve)
{
    if (requested == to_)
    {
        return;
    }

    // Two cases where a button has to be a cut: there is no picture yet to
    // dissolve from, and there is no shader to dissolve with.
    if (!haveFrame_ || !canDissolve)
    {
        cutTo(requested);
        return;
    }

    if (dissolve_.active())
    {
        if (requested == from_)
        {
            // Straight back the way it came. The blend on air is already the
            // right one; only the direction of travel changes.
            from_ = to_;
            to_   = requested;
            dissolve_.reverse();
            return;
        }

        // Aimed somewhere else mid-dissolve, which is what an operator does
        // when the first choice was wrong. What is on air is the composite, so
        // that is what has to be dissolved away from — starting from either
        // original source would snap the picture back to one the operator has
        // already left.
        if (presented_ && presented_ != composite_)
        {
            context.fullscreen->draw(*composite_, blit, presented_, constants,
                                     SamplerFilter::Point);
            from_ = ProgramSource::Composite;
            to_   = requested;
            dissolve_.snap(0.0f);
            return;
        }
    }

    from_ = to_;
    to_   = requested;
    dissolve_.snap(0.0f);
}

GpuTexture* ProgramOutput::render(EffectContext& context, GpuTexture* frame,
                                  bool inputHealthy, ProgramMode& mode)
{
    if (!live_[0] || !live_[1] || !black_ || !blackPixel_ || !mix_ || !composite_)
    {
        return nullptr;
    }
    const ShaderHandle blit = context.shaders->shader("passthrough");
    if (!blit)
    {
        return blackReady_ ? black_ : nullptr;
    }
    EffectConstants constants;
    setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);
    if (!blackReady_)
    {
        context.fullscreen->draw(*black_, blit, blackPixel_, constants, SamplerFilter::Point);
        blackReady_ = true;
    }

    const ShaderHandle mixShader = context.shaders->shader("crossfade");
    retarget(context, sourceFor(mode), blit, constants, mixShader != nullptr);
    dissolve_.advance(context.deltaTime);

    const bool dissolving = dissolve_.active();
    const bool liveWanted = to_ == ProgramSource::Live ||
                            (dissolving && from_ == ProgramSource::Live);
    const bool freezePinned = to_ == ProgramSource::Freeze ||
                              (dissolving && from_ == ProgramSource::Freeze);

    if (liveWanted)
    {
        if (inputHealthy && frame && frame->valid())
        {
            GpuTexture* target = live_[writeIndex_];
            context.fullscreen->draw(*target, blit, frame, constants, SamplerFilter::Point);
            liveCurrent_ = target;
            haveFrame_   = true;

            // The latched still stops moving exactly when the dissolve is
            // reading it — leaving Freeze or arriving at it. That is the same
            // invariant as everywhere else here: never write the texture the
            // current pass samples. Any other time it tracks, so a camera that
            // dies on the way back from Black freezes on the picture that was
            // just live rather than on one from before the fade.
            if (!freezePinned)
            {
                held_              = target;
                heldFraming_       = context.framing;
                heldFramingActive_ = context.framingActive;
                heldAspect_        = context.outputAspect;
                writeIndex_ ^= 1;
            }
        }
        else if (haveFrame_ && to_ == ProgramSource::Live)
        {
            // A fault, not a gesture. Safety does not dissolve: the last good
            // picture has to be on the wall this frame, not in 0.35 s.
            mode = ProgramMode::Freeze;
            cutTo(ProgramSource::Freeze);
        }
    }

    // PROGRAM preview must describe the held image, even while live tracking
    // and effects continue preparing the next shot behind Freeze or Black.
    if (haveFrame_)
    {
        context.framing       = heldFraming_;
        context.framingActive = heldFramingActive_;
        context.outputAspect  = heldAspect_;
    }
    else
    {
        presented_ = black_;
        return black_;
    }

    GpuTexture* target = sourceTexture(to_);
    if (!target || !target->valid())
    {
        target = black_;
    }

    GpuTexture* origin = dissolve_.active() ? sourceTexture(from_) : nullptr;
    if (!mixShader || !origin || !origin->valid() || origin == target)
    {
        // Nothing to blend, or nothing to blend with. Both ends of a finished
        // ramp are a single source, and that is the common case: steady FX,
        // Freeze and Black each cost no pass at all.
        presented_ = target;
        return target;
    }

    setParameterConstant(constants, 0, dissolve_.amount());
    context.fullscreen->draw(*mix_, mixShader, target, constants, SamplerFilter::Point, origin);
    presented_ = mix_;
    return mix_;
}

void ProgramOutput::shutdown()
{
    live_[0] = live_[1] = nullptr;
    liveCurrent_ = held_ = black_ = blackPixel_ = mix_ = composite_ = presented_ = nullptr;
    writeIndex_ = 0;
    from_ = to_ = ProgramSource::Live;
    dissolve_ = Dissolve{};
    haveFrame_ = blackReady_ = false;
    heldFraming_ = {};
    heldFramingActive_ = false;
    heldAspect_ = 16.0f / 9.0f;
}

} // namespace atemfx
