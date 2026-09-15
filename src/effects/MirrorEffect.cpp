#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// Reflects one half of the frame onto the other. Mode uses makeChoice so the
// generic UI draws a combo — no bespoke panel, and no M3 presets required.
class MirrorEffect final : public ShaderEffect
{
public:
    MirrorEffect()
        : ShaderEffect({"mirror", "Mirror", "Geometry",
                        "Reflects one half of the frame onto the other."},
                       "mirror",
                       SamplerFilter::Linear)
    {
        parameters_.add(Parameter::makeChoice("mode", "Mode", 0,
                                              {"Left → Right", "Right → Left",
                                               "Top → Bottom", "Bottom → Top", "Quad"}));
        parameters_.add(Parameter::makeFloat("pivot", "Pivot", 0.5f, 0.0f, 1.0f));
    }
};

} // namespace

std::unique_ptr<Effect> createMirrorEffect()
{
    return std::make_unique<MirrorEffect>();
}

} // namespace atemfx
