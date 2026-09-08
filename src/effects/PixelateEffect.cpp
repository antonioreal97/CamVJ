#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// Block quantisation. Point sampling on purpose: linear filtering would soften
// the block edges, which is the opposite of the intent.
class PixelateEffect final : public ShaderEffect
{
public:
    PixelateEffect()
        : ShaderEffect({"pixelate", "Pixelate", "Distort",
                        "Quantises the frame into square blocks."},
                       "pixelate",
                       SamplerFilter::Point)
    {
        parameters_.add(Parameter::makeInt("size", "Block Size", 16, 1, 256));
        parameters_.add(Parameter::makeFloat("mix", "Mix", 1.0f, 0.0f, 1.0f));
    }
};

} // namespace

std::unique_ptr<Effect> createPixelateEffect()
{
    return std::make_unique<PixelateEffect>();
}

} // namespace atemfx
