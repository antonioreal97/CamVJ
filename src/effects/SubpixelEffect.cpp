#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// Luminance-gated RGB cells that orbit inside the grid. Point sampling keeps
// the sprites hard-edged; a linear filter would blur the split. Scatter is
// local to the cell — a neighbourhood gather missed the 1080p60 budget.
class SubpixelEffect final : public ShaderEffect
{
public:
    SubpixelEffect()
        : ShaderEffect({"subpixel", "Subpixel", "Distort",
                        "Scatters RGB subpixels from a luminance grid."},
                       "subpixel",
                       SamplerFilter::Point)
    {
        parameters_.add(Parameter::makeInt("grid", "Grid", 18, 4, 128));
        parameters_.add(Parameter::makeFloat("threshold", "Threshold", 0.22f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("scatter", "Scatter", 0.35f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("rgb_spread", "RGB Spread", 0.45f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("noise_scale", "Noise Scale", 1.0f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("speed", "Speed", 0.60f, 0.0f, 4.0f));
        parameters_.add(Parameter::makeFloat("stretch", "Stretch", 0.0f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("mix", "Mix", 1.0f, 0.0f, 1.0f));
    }
};

} // namespace

std::unique_ptr<Effect> createSubpixelEffect()
{
    return std::make_unique<SubpixelEffect>();
}

} // namespace atemfx
