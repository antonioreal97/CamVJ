#include "overlays/overlay_compositor.h"

#include <algorithm>

namespace atemfx {

namespace {

const std::string kOverlayShader = "overlay_composite";

} // namespace

bool OverlayCompositor::initialize(EffectContext& context, std::string& error)
{
    shutdown();
    if (!context.shaders || !context.fullscreen || !context.targets)
    {
        error = "Overlay compositor needs rendering resources";
        return false;
    }

    if (!context.shaders->shader(kOverlayShader, &error))
    {
        return false;
    }

    targets_[0] = &context.targets->persistent("overlay.composite.a");
    targets_[1] = &context.targets->persistent("overlay.composite.b");
    if (!targets_[0]->valid() || !targets_[1]->valid() || targets_[0] == targets_[1])
    {
        error = "Failed to reserve overlay compositor targets";
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

GpuTexture& OverlayCompositor::composite(EffectContext& context, GpuTexture& input,
                                         std::span<const OverlayCompositeLayer> layers,
                                         float effectMix, bool bypass)
{
    lastPassCount_ = 0;
    const float cleanMix = std::clamp(effectMix, 0.0f, 1.0f);
    if (bypass || cleanMix <= 0.0f || layers.empty() || !initialized_ ||
        !context.shaders || !context.fullscreen)
    {
        return input;
    }

    // Resolve before drawing any layer. A failed hot reload must leave the
    // original picture intact, not return a half-composited stack.
    const ShaderHandle shader = context.shaders->shader(kOverlayShader);
    if (!shader)
    {
        return input;
    }

    GpuTexture* current = &input;
    std::size_t targetIndex = current == targets_[0] ? 1 : 0;

    for (const OverlayCompositeLayer& layer : layers)
    {
        if (!layer.texture || !layer.texture->valid())
        {
            continue;
        }

        const float opacity = std::clamp(layer.opacity, 0.0f, 1.0f) * cleanMix;
        if (opacity <= 0.0f)
        {
            continue;
        }

        GpuTexture* destination = targets_[targetIndex];
        if (!destination || !destination->valid() || destination == current ||
            destination == layer.texture)
        {
            targetIndex ^= 1;
            destination = targets_[targetIndex];
        }
        if (!destination || !destination->valid() || destination == current ||
            destination == layer.texture)
        {
            continue;
        }

        EffectConstants constants;
        setFrameConstants(constants, context.width, context.height,
                          context.time, context.deltaTime);
        setParameterConstant(constants, 0, opacity);
        setParameterConstant(constants, 1,
                             layer.aspect == OverlayAspect::Portrait9x16 ? 1.0f : 0.0f);

        // t0 is the overlay and t1 the picture below it. Alpha composition is
        // explicit in the shader because the RHI deliberately exposes no
        // backend blend-state surface.
        context.fullscreen->draw(*destination, shader, layer.texture, constants,
                                 SamplerFilter::Linear, current);
        current = destination;
        targetIndex ^= 1;
        ++lastPassCount_;
    }

    return *current;
}

void OverlayCompositor::shutdown()
{
    targets_[0] = targets_[1] = nullptr;
    initialized_ = false;
    lastPassCount_ = 0;
}

} // namespace atemfx
