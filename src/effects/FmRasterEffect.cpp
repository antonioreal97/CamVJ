#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// Scanlines whose phase is driven by source luminance. Linear sampling: the
// carrier is analog; point sampling would stair-step the modulation.
class FmRasterEffect final : public ShaderEffect
{
public:
    FmRasterEffect()
        : ShaderEffect({"fm_raster", "FM Raster", "Distort",
                        "Frequency-modulates scanlines with luminance."},
                       "fm_raster",
                       SamplerFilter::Linear)
    {
        parameters_.add(Parameter::makeFloat("frequency", "Frequency", 56.0f, 4.0f, 160.0f));
        parameters_.add(Parameter::makeFloat("modulation", "Modulation", 2.0f, 0.0f, 8.0f));
        parameters_.add(Parameter::makeFloat("threshold", "Threshold", 0.58f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("line_width", "Line Width", 0.08f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("direction", "Direction", 90.0f, 0.0f, 360.0f));
        parameters_.add(Parameter::makeFloat("speed", "Speed", 0.40f, 0.0f, 8.0f));
        parameters_.add(Parameter::makeFloat("contrast", "Contrast", 1.60f, 0.25f, 4.0f));
        parameters_.add(Parameter::makeFloat("mix", "Mix", 1.0f, 0.0f, 1.0f));
    }
};

} // namespace

std::unique_ptr<Effect> createFmRasterEffect()
{
    return std::make_unique<FmRasterEffect>();
}

} // namespace atemfx
