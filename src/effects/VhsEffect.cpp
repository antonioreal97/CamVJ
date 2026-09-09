#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// Tape artefacts, not display artefacts: `crt` draws the screen, this smears
// the signal that reaches it. Linear sampling because every artefact here is
// a sub-pixel horizontal displacement.
class VhsEffect final : public ShaderEffect
{
public:
    VhsEffect()
        : ShaderEffect({"vhs", "VHS", "Distort",
                        "Tape wobble, chroma bleed, dropouts and tracking noise."},
                       "vhs",
                       SamplerFilter::Linear)
    {
        parameters_.add(Parameter::makeFloat("wobble", "Wobble", 0.35f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("chroma_bleed", "Chroma Bleed", 0.55f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("noise", "Noise", 0.25f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("tracking", "Tracking", 0.25f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("dropouts", "Dropouts", 0.20f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("ghost", "Ghost", 0.20f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("interlace", "Interlace", 0.30f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("wear", "Wear", 0.45f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("speed", "Speed", 1.0f, 0.0f, 4.0f));
        parameters_.add(Parameter::makeFloat("mix", "Mix", 1.0f, 0.0f, 1.0f));
    }
};

} // namespace

std::unique_ptr<Effect> createVhsEffect()
{
    return std::make_unique<VhsEffect>();
}

} // namespace atemfx
