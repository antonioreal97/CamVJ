#include "video/program_output.h"

#include <algorithm>
#include <cstdint>

namespace atemfx {

namespace {

constexpr float kMinTransitionSeconds = 0.01f;

// Only Effects and Clean are ends of the dissolve. Freeze and Black are not
// somewhere between them: they replace the picture outright, so the ramp has
// no target to move toward and stays where the operator left it.
bool isMixEnd(ProgramMode mode)
{
    return mode == ProgramMode::Effects || mode == ProgramMode::Clean;
}

float endpointFor(ProgramMode mode)
{
    return mode == ProgramMode::Clean ? 0.0f : 1.0f;
}

} // namespace

void ProgramTransition::setDuration(float seconds)
{
    seconds_ = std::max(seconds, kMinTransitionSeconds);
}

void ProgramTransition::snapTo(ProgramMode mode)
{
    if (isMixEnd(mode))
    {
        phase_ = endpointFor(mode);
    }
}

float ProgramTransition::update(ProgramMode mode, float deltaTime)
{
    if (!isMixEnd(mode) || !(deltaTime > 0.0f))
    {
        return amount();
    }

    // A frame long enough to cross the whole ramp lands on the endpoint rather
    // than overshooting it: a dropped frame must not make the mix bounce.
    const float target = endpointFor(mode);
    const float step   = deltaTime / seconds_;
    phase_ = phase_ < target ? std::min(phase_ + step, target) : std::max(phase_ - step, target);
    return amount();
}

float ProgramTransition::amount() const
{
    const float phase = std::clamp(phase_, 0.0f, 1.0f);
    return phase * phase * (3.0f - 2.0f * phase);
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

    held_       = &context.targets->persistent("program.last");
    black_      = &context.targets->persistent("program.black");
    blackPixel_ = &context.targets->uploadTarget("program.pixel", 1, 1);
    const uint8_t pixel[] = {0, 0, 0, 255};
    if (!held_->valid() || !black_->valid() || !blackPixel_->valid() ||
        !context.targets->upload(*blackPixel_, pixel, sizeof(pixel)))
    {
        error = "Failed to prepare program safety images";
        shutdown();
        return false;
    }
    return true;
}

GpuTexture* ProgramOutput::render(EffectContext& context, GpuTexture* frame,
                                  bool inputHealthy, ProgramMode& mode)
{
    if (!held_ || !black_ || !blackPixel_)
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

    if (mode == ProgramMode::Effects || mode == ProgramMode::Clean)
    {
        if (inputHealthy && frame && frame->valid())
        {
            context.fullscreen->draw(*held_, blit, frame, constants, SamplerFilter::Point);
            haveFrame_          = true;
            heldFraming_        = context.framing;
            heldFramingActive_  = context.framingActive;
            heldAspect_         = context.outputAspect;
        }
        else if (haveFrame_)
        {
            mode = ProgramMode::Freeze;
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
    return mode == ProgramMode::Black || !haveFrame_ ? black_ : held_;
}

void ProgramOutput::shutdown()
{
    held_ = black_ = blackPixel_ = nullptr;
    haveFrame_ = blackReady_ = false;
    heldFraming_ = {};
    heldFramingActive_ = false;
    heldAspect_ = 16.0f / 9.0f;
}

} // namespace atemfx
