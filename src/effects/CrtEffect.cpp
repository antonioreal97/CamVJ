#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// Scanlines plus an RGB aperture grille. Linear sampling so the mask does not
// alias into a moire on the preview. Not a bloom: glow would need a downsample.
class CrtEffect final : public ShaderEffect
{
public:
    CrtEffect()
        : ShaderEffect({"crt", "CRT", "Distort",
                        "Scanlines, aperture grille and a light RGB split."},
                       "crt",
                       SamplerFilter::Linear)
    {
        parameters_.add(Parameter::makeFloat("scanlines", "Scanlines", 0.40f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("mask", "Mask", 0.35f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("aberration", "Aberration", 0.12f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("contrast", "Contrast", 1.15f, 0.5f, 2.0f));
        parameters_.add(Parameter::makeFloat("mix", "Mix", 1.0f, 0.0f, 1.0f));
    }
};

} // namespace

std::unique_ptr<Effect> createCrtEffect()
{
    return std::make_unique<CrtEffect>();
}

} // namespace atemfx
