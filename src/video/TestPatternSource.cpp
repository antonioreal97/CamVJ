#include "video/TestPatternSource.h"

#include <algorithm>

#include "effects/Effect.h"
#include "video/VideoDevices.h"

namespace atemfx {

namespace {

constexpr const char* kShaderName = "test_pattern";
constexpr const char* kTargetKey  = "source.frame";

} // namespace

TestPatternSource::TestPatternSource()
    : VideoSource({kTestPatternSourceId, "Test Pattern", "Internal"})
{
}

bool TestPatternSource::initialize(EffectContext& context, std::string& error)
{
    parameters_.add(Parameter::makeChoice("pattern", "Pattern", 0,
        {"Colour Bars", "Plasma", "Grid", "LED Mapping (16:9 + 9:16)"}));
    parameters_.add(Parameter::makeFloat("speed", "Speed", 1.0f, 0.0f, 4.0f));
    parameters_.add(Parameter::makeBool("markers", "Motion Markers", true));

    target_ = &context.targets->persistent(kTargetKey);
    if (!target_->valid())
    {
        error = "Failed to allocate the source target";
        return false;
    }

    return context.shaders->shader(kShaderName, &error) != nullptr;
}

void TestPatternSource::shutdown()
{
    target_ = nullptr;
}

GpuTexture* TestPatternSource::render(EffectContext& context)
{
    const ShaderHandle shader = context.shaders->shader(kShaderName);
    if (!shader || !target_ || !target_->valid())
    {
        return nullptr;
    }

    EffectConstants constants;
    setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);

    const std::vector<Parameter>& parameters = parameters_.all();
    const std::size_t             count      = std::min(parameters.size(), kMaxEffectParameters);
    for (std::size_t i = 0; i < count; ++i)
    {
        setParameterConstant(constants, i, parameters[i].value);
    }

    // A generator reads no source texture.
    context.fullscreen->draw(*target_, shader, nullptr, constants, SamplerFilter::Linear);

    return target_;
}

std::string TestPatternSource::status() const
{
    return bypassEffects()
        ? "LED mapping: 16:9 + centred 9:16. Auto Frame and FX bypassed; static pattern."
        : "internal generator";
}

bool TestPatternSource::bypassEffects() const
{
    return parameters_.valueOr("pattern", 0.0f) == 3.0f;
}

} // namespace atemfx
