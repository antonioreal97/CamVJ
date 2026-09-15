#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// Reflects one half of the frame onto the other. Mode is a named choice.
class MirrorEffect final : public ShaderEffect
{
public:
    MirrorEffect()
        : ShaderEffect({"mirror", "Mirror", "Geometry",
                        "Reflects one half of the frame onto the other."},
                       "mirror",
                       SamplerFilter::Linear)
    {
        parameters_.add(Parameter::makeChoice(
            "mode", "Mode", 0,
            {"Left > Right", "Right > Left", "Top > Bottom", "Bottom > Top", "Quad"}));
        parameters_.add(Parameter::makeFloat("pivot", "Pivot", 0.5f, 0.0f, 1.0f));
    }
};

} // namespace

std::unique_ptr<Effect> createMirrorEffect()
{
    return std::make_unique<MirrorEffect>();
}

} // namespace atemfx
