#include "effects/ShaderEffect.h"

#include <algorithm>

namespace atemfx {

ShaderEffect::ShaderEffect(EffectDescriptor descriptor, std::string shaderName, SamplerFilter filter)
    : Effect(std::move(descriptor))
    , shaderName_(std::move(shaderName))
    , filter_(filter)
{
}

bool ShaderEffect::initialize(EffectContext& context)
{
    lastError_.clear();

    // Compile now so a broken shader is reported at load, not at the moment
    // the operator enables the effect on air.
    return context.shaders->shader(shaderName_, &lastError_) != nullptr;
}

void ShaderEffect::shutdown()
{
}

void ShaderEffect::packConstants(const EffectContext& context, EffectConstants& constants) const
{
    setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);

    const std::vector<Parameter>& parameters = parameters_.all();
    const std::size_t             count      = std::min(parameters.size(), kMaxEffectParameters);
    for (std::size_t i = 0; i < count; ++i)
    {
        setParameterConstant(constants, i, parameters[i].currentValue());
    }
}

bool ShaderEffect::process(EffectContext& context, const GpuTexture& source, GpuTexture& destination)
{
    // Cheap cache lookup rather than a stored handle: hot reload replaces the
    // compiled shader, and a stale handle here would be a use-after-free.
    const ShaderHandle shader = context.shaders->shader(shaderName_);
    if (!shader)
    {
        return false;
    }

    // A hot reload can fix a shader that failed at start-up; clear the stale
    // error so the UI stops reporting a problem that no longer exists.
    if (!lastError_.empty())
    {
        lastError_.clear();
    }

    EffectConstants constants;
    packConstants(context, constants);

    context.fullscreen->draw(destination, shader, &source, constants, filter_);
    return true;
}

} // namespace atemfx
