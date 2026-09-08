#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// Chromatic separation: red and blue are displaced in opposite directions.
// Parameter order here is the packing order into uParams — see rgb_split.hlsl.
class RgbSplitEffect final : public ShaderEffect
{
public:
    RgbSplitEffect()
        : ShaderEffect({"rgb_split", "RGB Split", "Distort",
                        "Displaces the red and blue channels."},
                       "rgb_split",
                       SamplerFilter::Linear)
    {
        parameters_.add(Parameter::makeFloat("amount", "Amount", 0.20f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("angle", "Angle", 0.0f, 0.0f, 360.0f));
        parameters_.add(Parameter::makeBool("radial", "Radial", false));
    }
};

} // namespace

std::unique_ptr<Effect> createRgbSplitEffect()
{
    return std::make_unique<RgbSplitEffect>();
}

} // namespace atemfx
